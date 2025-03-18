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
//#include <linux/pmlab.h> // Included in linux/sched.h

// These aren't defined in the Linux headers, so we define them ourselves:
#define MSR_ARCH_PERFMON_EVENTSEL2 0x188
#define MSR_ARCH_PERFMON_EVENTSEL3 0x189
#define MSR_ARCH_PERFMON_PERFCTR2 0xc3
#define MSR_ARCH_PERFMON_PERFCTR3 0xc4

#define MEM_INST_RETIRED_ALL_LOADS   0x81d0
#define ICACHE_ACCESSES              0x0380
#define TOPDOWN_RETIRING_ALL         0x00c2
#define UNC_M_CLOCKTICKS             0x0001
// Event number + umask for L3 cache misses
#define LONGEST_LAT_CACHE_MISS       0x412e
// Event number + umask for branch misses
#define BR_MISP_RETIRED_ALL_BRANCHES 0x00c5

#define NUM_ENERGY_COUNTERS 5

#define EFFICIENCY_CORE  0
#define PERFORMANCE_CORE 1

const s64 energy_model_factors[NUM_ENERGY_COUNTERS + 1] = {
	-40, 377, 212, 91, 1451, 5671202142645 - 2140000000000, // efficiency core
	// last element is intercept
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
	rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR0, mea->counters[0]);
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR0,   mea->counters[1]);
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR1,   mea->counters[2]);
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR2,   mea->counters[3]);
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR3,   mea->counters[4]);
}

static u64
evaluate_energy_model(const struct measurement *start, const struct measurement *end)
{
	s64 energy_sum = energy_model_factors[NUM_ENERGY_COUNTERS];
	for (unsigned i = 0; i < NUM_ENERGY_COUNTERS; i++) {
		// Unsigned differentiation handles counter wrap-around
		u64 delta = end->counters[i] - start->counters[i];
		// Signed dot product allows for negative factors
		energy_sum += energy_model_factors[i] * (s64)delta;
	}
	// Total sum should not be negative, even if some products are
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

	// Simply enable all fixed and programmable counters
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0x7000000fful);
	// Configure the individual event counters
	wrmsrl(MSR_ARCH_PERFMON_FIXED_CTR_CTRL, INTEL_FIXED_0_KERNEL | INTEL_FIXED_0_USER);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL0, MEM_INST_RETIRED_ALL_LOADS | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_OS | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL1, ICACHE_ACCESSES | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_OS | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL2, TOPDOWN_RETIRING_ALL | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_OS | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL3, UNC_M_CLOCKTICKS | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_OS | ARCH_PERFMON_EVENTSEL_ENABLE);

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

	u64 power;
	if (total_duration > 0) { // Don't divide by zero
		// pWs / ns = mW
		// TODO proper rounding
		power = total_energy / total_duration;
	} else {
		power = 0;
	}

	return power;
}
