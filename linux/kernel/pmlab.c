/* Power Management Lab */

#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/percpu-defs.h>
#include <linux/spinlock.h>
#include <asm/msr.h> // Needed to access performance counters through MSRs
//#include <linux/pmlab.h> // Included in linux/sched.h

// We use this variable to store the last known counter value on this CPU.
DEFINE_PER_CPU(u64, power_events_checkpoint);

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
		em->power -= em->samples[index].scaled_value;

		em->first_sample++;
		if (em->first_sample >= MAX_ENERGY_SAMPLES) {
			em->first_sample -= MAX_ENERGY_SAMPLES;
		}
	}

	em->samples[index] = sample;
	em->power += sample.scaled_value;
}

/* Pop the oldest sample off the ring buffer, and update the total estimate accordingly.
 * em->lock must be held when calling this function.
 */
static void
drop_energy_sample(struct energy_model *em)
{
	if (em->num_samples == 0) return;

	em->power -= em->samples[em->first_sample].scaled_value;

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
	spin_lock_init(&em->lock);
	em->power        = 0;
	em->first_sample = 0;
	em->num_samples  = 0;
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

	struct energy_model *em = &prev->energy_model;

	unsigned long cpu_flags;
	spin_lock_irqsave(&em->lock, cpu_flags);

	u64 difference = end_count - start_count;
	struct energy_sample sample;
	sample.scaled_value = difference; // TODO scale by elapsed time
	push_energy_sample(em, sample);

	spin_unlock_irqrestore(&em->lock, cpu_flags);
}

u64
pmlab_power_consumption_of_task(struct task_struct *tsk)
{
	struct energy_model *em = &tsk->energy_model;

	unsigned long cpu_flags;
	spin_lock_irqsave(&em->lock, cpu_flags);

	u64 power = em->power;

	spin_unlock_irqrestore(&em->lock, cpu_flags);

	return power;
}
