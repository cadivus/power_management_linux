/* Power Management Lab */

#include <linux/printk.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/percpu-defs.h>
#include <linux/spinlock.h>
#include <linux/timekeeping.h>
#include <asm/msr.h> // Needed to access performance counters through MSRs
//#include <linux/pmlab.h> // Included in linux/sched.h

DEFINE_PER_CPU(u64, pmsample_start_count);
DEFINE_PER_CPU(u64, pmsample_start_time);

/* Push a new sample onto the ring buffer, and update the total estimate accordingly.
 * em->lock must be held when calling this function.
 */
static void
push_energy_sample(struct energy_model *em, struct energy_sample sample)
{
	int index = em->first_sample + em->num_samples;
	if (index >= MAX_ENERGY_SAMPLES) {
		index -= MAX_ENERGY_SAMPLES;
	}
	
	if (em->num_samples < MAX_ENERGY_SAMPLES) {
		em->num_samples++;
	} else {
		em->total_energy   -= em->samples[index].energy;
		em->total_duration -= em->samples[index].duration;

		em->first_sample++;
		if (em->first_sample >= MAX_ENERGY_SAMPLES) {
			em->first_sample -= MAX_ENERGY_SAMPLES;
		}
	}

	em->samples[index] = sample;
	em->total_energy   += sample.energy;
	em->total_duration += sample.duration;
}

/* Pop the oldest sample off the ring buffer, and update the total estimate accordingly.
 * em->lock must be held when calling this function.
 */
static void
drop_energy_sample(struct energy_model *em)
{
	if (em->num_samples == 0) return;

	em->total_energy   -= em->samples[em->first_sample].energy;
	em->total_duration -= em->samples[em->first_sample].duration;

	em->first_sample++;
	if (em->first_sample >= MAX_ENERGY_SAMPLES) {
		em->first_sample -= MAX_ENERGY_SAMPLES;
	}
	em->num_samples--;
}

void
pmlab_reset_task_struct(struct task_struct *tsk)
{
	struct energy_model *em = &tsk->energy_model;
	memset(em, 0, sizeof *em);
	spin_lock_init(&em->lock);
}

void
pmlab_install_performance_counters(void)
{
	u64 proc_id = smp_processor_id();
	printk("PML: Installing power performance counters on processor %llu.\n", proc_id);
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0x7000000fful);
	wrmsrl(MSR_P6_EVNTSEL0, 0x004300c0);

	u64 start_count;
	rdmsrl(MSR_P6_PERFCTR0, start_count);

	u64 start_time = ktime_get_ns();

	get_cpu_var(pmsample_start_count) = start_count;
	put_cpu_var(pmsample_start_count);

	get_cpu_var(pmsample_start_time) = start_time;
	put_cpu_var(pmsample_start_time);
}

void
pmlab_update_after_timeslice(struct task_struct *prev)
{
	u64 end_count;
	rdmsrl(MSR_P6_PERFCTR0, end_count);

	u64 end_time = ktime_get_ns();

	u64 start_count = get_cpu_var(pmsample_start_count);
	get_cpu_var(pmsample_start_count) = end_count;
	put_cpu_var(pmsample_start_count);

	u64 start_time = get_cpu_var(pmsample_start_time);
	get_cpu_var(pmsample_start_time) = end_time;
	put_cpu_var(pmsample_start_time);

	u64 delta_count = end_count - start_count;
	u64 delta_time  = end_time  - start_time;

	struct energy_model *em = &prev->energy_model;
	unsigned long cpu_flags;
	spin_lock_irqsave(&em->lock, cpu_flags);

	struct energy_sample sample = {
		.energy   = delta_count,
		.duration = delta_time
	};
	push_energy_sample(em, sample);

	spin_unlock_irqrestore(&em->lock, cpu_flags);
}

u64
pmlab_power_consumption_of_task(struct task_struct *tsk)
{
	struct energy_model *em = &tsk->energy_model;

	unsigned long cpu_flags;
	spin_lock_irqsave(&em->lock, cpu_flags);

	u64 total_energy   = em->total_energy;
	u64 total_duration = em->total_duration;

	spin_unlock_irqrestore(&em->lock, cpu_flags);

	u64 power = total_energy;
	if (total_duration > 0) { // Don't divide by zero
		// Perform the following calculation with 128 bit precision:
		// power = energy / (duration / 10^9)
		const u64 ns_per_sec = 1000000000ul;
		u64 hi = 0;
		__asm__ (
			"mulq	%2\n\t"
			"divq	%3"
			: "+a"(power), "+d"(hi)
			: "rm"(ns_per_sec), "rm"(total_duration)
			: "cc");
	}

	return power;
}
