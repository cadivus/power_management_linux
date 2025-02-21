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

#define NUM_ENERGY_COUNTERS 1

const s64 energy_model_factors[NUM_ENERGY_COUNTERS] = {
	1000
};

struct measurement {
	u64 time;
	u64 counters[NUM_ENERGY_COUNTERS];
};

DEFINE_PER_CPU(struct measurement, pmlab_latest);

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

static void
conduct_measurement(struct measurement *mea)
{
	mea->time = ktime_get_ns();
	rdmsrl(MSR_P6_PERFCTR0, mea->counters[0]);
}

static u64
evaluate_energy_model(const struct measurement *start, const struct measurement *end)
{
	s64 energy_sum = 0;
	for (unsigned i = 0; i < NUM_ENERGY_COUNTERS; i++) {
		// Unsigned differentiation handles counter wrap-around
		u64 delta = end->counters[i] - start->counters[i];
		// Signed dot product allows for negative factors
		energy_sum += energy_model_factors[i] * (s64)delta;
	}
	// Total sum should not be negative, even if some product are
	return energy_sum < 0 ? 0 : (u64)energy_sum;
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

	struct measurement *latest = &get_cpu_var(pmlab_latest);
	conduct_measurement(latest);
	put_cpu_var(pmlab_latest);
}

void
pmlab_update_after_timeslice(struct task_struct *prev)
{
	struct measurement *latest = &get_cpu_var(pmlab_latest);
	struct measurement start = *latest;
	conduct_measurement(latest);

	struct energy_sample sample;
	sample.duration = latest->time - start.time;
	sample.energy   = evaluate_energy_model(&start, latest);

	put_cpu_var(pmlab_latest);

	struct energy_model *em = &prev->energy_model;
	unsigned long cpu_flags;
	spin_lock_irqsave(&em->lock, cpu_flags);

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
