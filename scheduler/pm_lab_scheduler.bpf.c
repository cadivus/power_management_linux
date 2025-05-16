#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define SHARED_DSQ_ID 0
#define SHARED_DSQ_KERNEL_ID 1

#define BPF_STRUCT_OPS(name, args...)    \
    SEC("struct_ops/"#name) BPF_PROG(name, ##args)

#define BPF_STRUCT_OPS_SLEEPABLE(name, args...)    \
    SEC("struct_ops.s/"#name) BPF_PROG(name, ##args)

// New names from 6.13 for clarity.
#define scx_bpf_dsq_insert          scx_bpf_dispatch
#define scx_bpf_dsq_insert_vtime    scx_bpf_dispatch_vtime
#define scx_bpf_dsq_move_to_local   scx_bpf_consume
#define scx_bpf_dsq_move            scx_bpf_dispatch_from_dsq
#define scx_bpf_dsq_move_vtime      scx_bpf_dispatch_vtime_from_dsq


// Define a lock structure that contains a spin lock.
struct lock_data {
    struct bpf_spin_lock semaphore;
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, int);
    __type(value, struct lock_data);
} lock_map SEC(".maps");

// Shared power entries map: one entry per CPU (16 total)
struct cpu_power_entry {
    pid_t pid;
    u64 power;
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 16);
    __type(key, int);
    __type(value, struct cpu_power_entry);
} power_entries_map SEC(".maps");

// Global sum map to store a shared integer (sum of power consumption)
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, int);
    __type(value, u64);
} global_sum_map SEC(".maps");

u64 wattage_limit() {
    return 80;
}

// Scheduler initialization: create DSQs.
s32 BPF_STRUCT_OPS_SLEEPABLE(sched_init) {
    s32 success = 0;

    success |= scx_bpf_create_dsq(SHARED_DSQ_ID, -1);
    success |= scx_bpf_create_dsq(SHARED_DSQ_KERNEL_ID, -1);

    return success;
}

// Enqueue a task into the DSQ with a computed time slice.
int BPF_STRUCT_OPS(sched_enqueue, struct task_struct *p, u64 enq_flags) {
    if (p->mm) {
        u64 slice = 5000000u / scx_bpf_dsq_nr_queued(SHARED_DSQ_ID);
        scx_bpf_dsq_insert(p, SHARED_DSQ_ID, slice, enq_flags);
    } else {
        u64 slice = 5000000u / scx_bpf_dsq_nr_queued(SHARED_DSQ_KERNEL_ID);
        scx_bpf_dsq_insert(p, SHARED_DSQ_KERNEL_ID, slice, enq_flags);
    }

    return 0;
}

// Dispatch a task from the DSQ to a CPU.
int BPF_STRUCT_OPS(sched_dispatch, s32 cpu, struct task_struct *prev) {
    int key = 0;
    u64 cpu_cutoff_index = 0;
    u64 *value = bpf_map_lookup_elem(&global_sum_map, &key);
    if (value) {
        //bpf_printk("global sum = %llu\n", *value);
        cpu_cutoff_index = *value;
    }

    if (cpu <= cpu_cutoff_index) {
        scx_bpf_dsq_move_to_local(SHARED_DSQ_ID);
        scx_bpf_dsq_move_to_local(SHARED_DSQ_KERNEL_ID);
    } else {
        scx_bpf_dsq_move_to_local(SHARED_DSQ_KERNEL_ID);
    }
    return 0;
}

// Running hook: update the current CPU's entry under a spin lock, then print a snapshot.
int BPF_STRUCT_OPS(sched_running, struct task_struct *p) {
    int cpu = bpf_get_smp_processor_id();

    // Lookup the current CPUs entry
    int cpu_key = cpu;
    struct cpu_power_entry *my_entry = bpf_map_lookup_elem(&power_entries_map, &cpu_key);
    if (!my_entry)
        return 0;

    // Look-up the spin lock
    int lock_key = 0;
    struct lock_data *lock = bpf_map_lookup_elem(&lock_map, &lock_key);
    if (!lock)
        return 0;


    // Has to be called outside the lock
    u64 consumer_power = pmlab_power_consumption_of_task(p);

    // Acquire lock and update the current CPU's entry
    bpf_spin_lock(&lock->semaphore);
    my_entry->pid = p->pid;
    my_entry->power = consumed_power;
    bpf_spin_unlock(&lock->semaphore);

    // Snapshot: Copy all entries out of the map outside the lock
    struct cpu_power_entry snapshot[16];
    #pragma unroll
    for (int i = 0; i < 16; i++) {
        int idx = i;
        struct cpu_power_entry *entry = bpf_map_lookup_elem(&power_entries_map, &idx);
        if (entry) {
            snapshot[i] = *entry;
        } else {
            snapshot[i].pid = 0;
            snapshot[i].power = 0;
        }
    }

    /* --- Print the snapshot --- */
    #pragma unroll
    for (int i = 0; i < 16; i++) {
        //bpf_printk("Entry[%d]: pid = %d, watts = %llu\n",
        //           i, snapshot[i].pid, snapshot[i].power);
    }

    /* --- Calculate and update the global shared integer --- */
    u64 global_sum = 0;
    u64 max_processor_id = 16 - 1;
    #pragma unroll
    for (int i = 0; i < 16; i++) {
        global_sum += snapshot[i].power;

        if (global_sum > wattage_limit()) {
            max_processor_id = i;
            break;
        }
    }
    int sum_key = 0;
    bpf_map_update_elem(&global_sum_map, &sum_key, &max_processor_id, BPF_ANY);
    //bpf_printk("global max processor id = %llu\n", max_processor_id);
    //bpf_printk("running on %d\n", cpu);
    //bpf_printk("\n\n\n");

    return 0;
}

// Stopping hook: clear the power entry for the current CPU.
int BPF_STRUCT_OPS(sched_stopping, struct task_struct *p, bool runnable) {
    int cpu = bpf_get_smp_processor_id();
    struct cpu_power_entry *entry = bpf_map_lookup_elem(&power_entries_map, &cpu);

    if (entry) {
        entry->pid = 0;
        entry->power = 0;
    }

    return 0;
}

SEC(".struct_ops.link")
struct sched_ext_ops sched_ops = {
    .enqueue   = (void *)sched_enqueue,
    .dispatch  = (void *)sched_dispatch,
    .init      = (void *)sched_init,
    .flags     = SCX_OPS_ENQ_LAST | SCX_OPS_KEEP_BUILTIN_IDLE,
    .running   = (void *)sched_running,
    .stopping  = (void *)sched_stopping,
    .name      = "pmlab_power_limiting_scheduler"
};

char _license[] SEC("license") = "GPL";
