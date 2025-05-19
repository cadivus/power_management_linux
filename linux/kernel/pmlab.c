/* Power Management Lab */

#include <linux/printk.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/percpu-defs.h>
#include <linux/spinlock.h>
#include <linux/timekeeping.h>
#include <asm/msr.h>
#include <asm/perf_event.h>

// For eBPF interface
#include <linux/bpf_verifier.h>
#include <linux/btf.h>
#include <linux/bpf.h>

// These aren't defined in the Linux headers, so we define them ourselves:
#define MSR_ARCH_PERFMON_EVENTSEL2 0x188
#define MSR_ARCH_PERFMON_EVENTSEL3 0x189
#define MSR_ARCH_PERFMON_EVENTSEL4 0x18A
#define MSR_ARCH_PERFMON_EVENTSEL5 0x18B
#define MSR_ARCH_PERFMON_PERFCTR2 0xc3
#define MSR_ARCH_PERFMON_PERFCTR3 0xc4
#define MSR_ARCH_PERFMON_PERFCTR4 0xc5
#define MSR_ARCH_PERFMON_PERFCTR5 0xc6
#define IA32_PERF_GLOBAL_INUSE 0x392

// Perfmon event numbers (low byte) with umasks (high byte).
// Different CPU types may use different event numbers for the same event.
// Perf cores are CORE type, efficiency cores are ATOM type.
#define ATOM_instructions        0x00c0
#define ATOM_L1_dcache_loads     0x81d0
#define ATOM_L1_icache_loads     0x0380
#define ATOM_iTLB_load_misses    0x0481
#define ATOM_bus_cycles          0x013c
#define ATOM_cpu_cycles          0x003c
#define ATOM_ref_cycles          0x013c
#define ATOM_topdown_retiring    0x00c2
#define CORE_instructions        0x00c0
#define CORE_bus_cycles          0x013c
#define CORE_mem_stores          0x02cd
#define CORE_ref_cycles          0x013c
#define CORE_L1_dcache_loads     0x81d0
#define CORE_branch_instructions 0x00c4
#define CORE_cpu_cycles          0x003c
#define CORE_slots               0x01a4

struct energy_model_def {
	struct energy_term {
		unsigned event;  // Event ID with UMask
		int is_squared;  // Either linear (0) or squared (1) contribution.
		s64 coefficient; // Regression coefficient, scaled up by 10^12 for linear terms, and by 10^(12+9) for squared terms.
	} terms[NUM_ENERGY_COUNTERS];
	s64 intercept; // in mW
};

const struct energy_model_def energy_model_defs[2] = {
	// efficiency core model
	[EFFICIENCY_CORE] = {
		.terms = {
			{ ATOM_L1_dcache_loads,     0, 305 },
			{ ATOM_L1_icache_loads,     0, 47 },
			{ ATOM_cpu_cycles,          1, -1622 },
			{ ATOM_instructions,        0, -189 },
			{ ATOM_ref_cycles,          0, 5001 },
			{ ATOM_topdown_retiring,    0, 267 },
		},
		.intercept = 0,
	},
	// performance core model
	[PERFORMANCE_CORE] = {
		.terms = {
			{ CORE_L1_dcache_loads,     0, 789 },
			{ CORE_branch_instructions, 0, 332 },
			{ CORE_cpu_cycles,          0, 1165 },
			{ CORE_instructions,        0, 31 },
			{ CORE_ref_cycles,          0, 666 },
			{ CORE_slots,               0, 109 }
		},
		.intercept = 0,
	},
};

DEFINE_PER_CPU(struct energy_counts, pmlab_previous_counts);
DEFINE_PER_CPU(int, pmlab_core_type);

static inline void
add_s128(s64 dest[2], const s64 src[2])
{
	__asm__ ("addq %2, %0\n\tadcq %3, %1"
		: "+r"(dest[0]), "+r"(dest[1])
		: "rm"(src[0]), "rm"(src[1])
		: "cc");
}

static inline void
mul_s64_s128(s64 dest[2], s64 a, s64 b)
{
	s64 low = a, high = 0;
	__asm__ ("imulq %2"
		: "+a"(low), "=d"(high)
		: "rm"(b)
		: "cc");
	dest[0] = low;
	dest[1] = high;
}

static inline s64
div_s128_s64(const s64 num[2], s64 denom)
{
	s64 low = num[0], high = num[1];
	__asm__ ("idivq %2"
		: "+a"(low), "+d"(high)
		: "rm"(denom)
		: "cc");
	return low;
}

// Calling this function only makes sense if preemption is disabled.
static inline int
my_core_type(void)
{
	return *this_cpu_ptr(&pmlab_core_type);
}

static void
gather_energy_counts(struct energy_counts *ec)
{
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR0,   ec->counters[0]);
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR1,   ec->counters[1]);
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR2,   ec->counters[2]);
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR3,   ec->counters[3]);
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR4,   ec->counters[4]);
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR5,   ec->counters[5]);
}

static void
accumulate_energy_counts(struct task_struct *tsk, const struct energy_counts *start, const struct energy_counts *end)
{
	struct energy_model *em = &tsk->energy_model;

	// Compute elapsed time + increases in counters.
	// Unsigned differentiation handles counter wrap-around.
	struct energy_counts delta;
	for (unsigned i = 0; i < NUM_ENERGY_COUNTERS; i++) {
		delta.counters[i] = end->counters[i] - start->counters[i];
	}

	// Look out for unsigned integer overflows
	int overflowing = 0;
	for (unsigned i = 0; i < NUM_ENERGY_COUNTERS; i++) {
		overflowing = overflowing || (em->counts.counters[i] + delta.counters[i] < em->counts.counters[i]);
	}

	if (overflowing) {
		// If we would overflow the counters, just reset the energy model.
		// Since we have to reset the energy model anyhow every time the process
		// is rescheduled to a different CPU type, this should not introduce additional problems.
		printk("PMLab: resetting event counts for pid %llu, as they grew too large.\n", (long long unsigned)tsk->pid);
		memset(&em->counts, 0, sizeof em->counts);
		em->start_time = ktime_get_ns();
	}

	// Add to running sum
	for (unsigned i = 0; i < NUM_ENERGY_COUNTERS; i++) {
		em->counts.counters[i] += delta.counters[i];
	}
}

static u64
evaluate_power_consumption(const struct energy_model *em)
{
	u64 duration_ns = ktime_get_ns() - em->start_time;
	if (duration_ns == 0) {
		duration_ns = 1;
	}

	const struct energy_model_def *model = &energy_model_defs[em->core_type];
	// x86-64 can easily do 128-bit arithmatic.
	// We use this extra precision here to avoid overflows
	// from multiplications.
	s64 estimate[2] = { 0, 0 };

	// Sum up all contributions for squared terms and divide them through duration.
	// After the linear terms, this value will be divided again,
	// so for squared terms we effectively compute: count * count / time / time = (count / time)^2.
	for (unsigned i = 0; i < NUM_ENERGY_COUNTERS; i++) {
		const struct energy_term *term = &model->terms[i];
		if (!term->is_squared) continue;

		s64 magnitude = (s64)em->counts.counters[i];
		s64 contrib[2] = { 0, 0 };
		mul_s64_s128(contrib, term->coefficient * magnitude, magnitude);
		add_s128(estimate, contrib);
	}
	if (!(estimate[0] == 0 && estimate[1] == 0)) {
		estimate[0] = div_s128_s64(estimate, duration_ns);
		estimate[1] = estimate[0] < 0 ? -1 : 0; // sign extend into upper half
	}

	// Sum up all contributions for linear terms and divide them through duration.
	for (unsigned i = 0; i < NUM_ENERGY_COUNTERS; i++) {
		const struct energy_term *term = &model->terms[i];
		if (term->is_squared) continue;

		s64 magnitude = (s64)em->counts.counters[i];
		s64 contrib[2] = { 0, 0 };
		mul_s64_s128(contrib, term->coefficient, magnitude);
		add_s128(estimate, contrib);
	}
	estimate[0] = div_s128_s64(estimate, duration_ns); // mW = pJ / ns

	// Finally, add the intercept
	estimate[0] += model->intercept;

	// While individual contributions may be negative,
	// the final estimate should not be.
	return estimate[0] >= 0 ? estimate[0] : 0;
}

void
pmlab_reset_task_struct(struct task_struct *tsk)
{
	// Task structs are copied on fork().
	// We don't want our energy model to be copied,
	// so we have to reset its contents here.
	struct energy_model *em = &tsk->energy_model;
	memset(em, 0, sizeof *em);
	em->start_time = ktime_get_ns();
	// Doesn't matter if this guess is wrong,
	// since this will get updated after the first timeslice.
	em->core_type = EFFICIENCY_CORE;
	spin_lock_init(&em->lock);
}

void
pmlab_install_performance_counters(void)
{
	int proc_id = smp_processor_id();

	// Distinguish efficiency <-> performance cores
	uint32_t eax = 0x1A;
	__asm__ ("cpuid\n\t" : "+a"(eax) :: "ebx", "ecx", "edx"); // Get EAX of Leaf 0x1A
	unsigned core_family = eax >> 24;
	int *core_type_ptr = &get_cpu_var(pmlab_core_type);
	if (core_family == 0x20) { // Intel Atom (Efficiency Core)
		*core_type_ptr = EFFICIENCY_CORE;
	} else { // Intel Core (Performance Core)
		*core_type_ptr = PERFORMANCE_CORE;
	}
	put_cpu_var(pmlab_core_type);

	int core_type = my_core_type();

	printk("PMLab: Installing power performance counters on %c processor %d.\n",
		core_type == EFFICIENCY_CORE ? 'E' : 'P', proc_id);

	// Enable event counting
	u64 global_ctrl;
	rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, global_ctrl);
	global_ctrl |= 0x3full; // enable 6 programmable counters
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, global_ctrl);

	// Log if someone other than us is already using the perf counters
	const u64 my_counters_mask = 0x000000000000003ful;
	u64 others_counters_mask;
	rdmsrl(IA32_PERF_GLOBAL_INUSE, others_counters_mask);
	if (my_counters_mask & others_counters_mask) {
		printk("PMLab: core %d: clashing PMC counter usage: we need %llx, others use %llx.", proc_id, my_counters_mask, others_counters_mask);
	}

	// I couldn't disable the watchdog and perf entirely, so something in the Linux kernel
	// continously overwrites our attempts at managing the fixed counters.
	// This is why we only used programmable counters anymore.
#if 0
	// Enable the fixed event counter(s)
	u64 fixed_ctr_ctrl;
	rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR_CTRL, fixed_ctr_ctrl);
	fixed_ctr_ctrl &= ~INTEL_FIXED_BITS_MASK;
	fixed_ctr_ctrl |= INTEL_FIXED_0_USER; // configure instructions-retired
	wrmsrl(MSR_ARCH_PERFMON_FIXED_CTR_CTRL, fixed_ctr_ctrl);
#endif

	// Configure the programmable event counters
#if 0
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL0, energy_model_defs[core_type].terms[0].event | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_OS | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL1, energy_model_defs[core_type].terms[1].event | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_OS | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL2, energy_model_defs[core_type].terms[2].event | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_OS | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL3, energy_model_defs[core_type].terms[3].event | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_OS | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL4, energy_model_defs[core_type].terms[4].event | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_OS | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL5, energy_model_defs[core_type].terms[5].event | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_OS | ARCH_PERFMON_EVENTSEL_ENABLE);
#else
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL0, energy_model_defs[core_type].terms[0].event | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL1, energy_model_defs[core_type].terms[1].event | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL2, energy_model_defs[core_type].terms[2].event | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL3, energy_model_defs[core_type].terms[3].event | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL4, energy_model_defs[core_type].terms[4].event | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL5, energy_model_defs[core_type].terms[5].event | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
#endif

	// Gather initial counts on this CPU core
	struct energy_counts *latest = &get_cpu_var(pmlab_previous_counts);
	gather_energy_counts(latest);
	put_cpu_var(pmlab_previous_counts);
}

void
pmlab_update_after_timeslice(struct task_struct *prev, struct task_struct *next)
{
	unsigned long cpu_flags;

	// Sample event counters since the last reschedule.
	struct energy_counts *previous = &get_cpu_var(pmlab_previous_counts);
	struct energy_counts end, start = *previous;
	gather_energy_counts(&end);
	*previous = end;
	put_cpu_var(pmlab_previous_counts);

	// Update the energy model of the task that has just finished its timeslice.
	spin_lock_irqsave(&prev->energy_model.lock, cpu_flags);
	accumulate_energy_counts(prev, &start, &end);
	spin_unlock_irqrestore(&prev->energy_model.lock, cpu_flags);

	// If the upcoming task was rescheduled from a different CPU type,
	// throw out the data we have collected, since it is CPU-type specific.
	int core_type = my_core_type();
	spin_lock_irqsave(&next->energy_model.lock, cpu_flags);
	if (next->energy_model.core_type != core_type) {
		memset(&next->energy_model.counts, 0, sizeof next->energy_model.counts);
		next->energy_model.start_time = ktime_get_ns();
		next->energy_model.core_type = core_type;
	}
	spin_unlock_irqrestore(&next->energy_model.lock, cpu_flags);
}

__bpf_kfunc u64
pmlab_power_consumption_of_task(struct task_struct *tsk)
{
	struct energy_model *em = &tsk->energy_model;
	unsigned long cpu_flags;
	spin_lock_irqsave(&em->lock, cpu_flags);
	u64 power_mW = evaluate_power_consumption(em);
	spin_unlock_irqrestore(&em->lock, cpu_flags);
	return power_mW;
}


// Expose the pmlab_power_consumption_of_task function to ebpf programs
BTF_KFUNCS_START(bpf_pmlab_set)
BTF_ID_FLAGS(func, pmlab_power_consumption_of_task)
BTF_KFUNCS_END(bpf_pmlab_set)

static const struct btf_kfunc_id_set bpf_pmlab_kfunc_set = {
        .owner = THIS_MODULE,
        .set   = &bpf_pmlab_set,
};

static int init_subsystem(void)
{
	int ret;

	if (
		(ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACING, &bpf_pmlab_kfunc_set))
		|| (ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_SYSCALL, &bpf_pmlab_kfunc_set))
		|| (ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &bpf_pmlab_kfunc_set))
	) {
		pr_err("pmlab: Failed to register kfunctions");
		return ret;
	}

	return 0;
}

late_initcall(init_subsystem);
