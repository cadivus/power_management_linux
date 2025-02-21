/* Power Management Lab */

#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/percpu-defs.h>
#include <asm/msr.h> // Needed to access performance counters through MSRs
//#include <linux/pmlab.h> // Included in linux/sched.h

// We use this variable to store the last known counter value on this CPU.
DEFINE_PER_CPU(u64, power_events_checkpoint);

void
pmlab_reset_task_struct(struct task_struct *tsk)
{
	tsk->consumed_power = 0;
}

void
pmlab_install_performance_counters(void)
{
	u64 proc_id = smp_processor_id();
	printk("PML: Installing power performance counters on processor %llu.\n", proc_id);
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0x7000000fful);
	wrmsrl(MSR_P6_EVNTSEL0, 0x004300c0);

	u64 base_count;
	rdmsrl(MSR_P6_PERFCTR0, base_count);

	get_cpu_var(power_events_checkpoint) = base_count;
	put_cpu_var(power_events_checkpoint);
}

void
pmlab_update_after_timeslice(struct task_struct *prev)
{
	u64 end_count;
	rdmsrl(MSR_P6_PERFCTR0, end_count);

	u64 start_count = get_cpu_var(power_events_checkpoint);
	get_cpu_var(power_events_checkpoint) = end_count;
	put_cpu_var(power_events_checkpoint);

	/* If we switch away from a userspace task, update its
	 * performance measurements before switching.
	 */
	if (prev->mm) { // from user
		u64 difference = end_count - start_count;
		prev->consumed_power += difference;
	}
}

