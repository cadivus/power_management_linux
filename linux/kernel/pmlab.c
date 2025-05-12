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

// Perfmon event numbers + umasks
#define MEM_INST_RETIRED_ALL_LOADS   0x81d0 // maps to dTLB-loads
#define ICACHE_ACCESSES              0x0380
#define TOPDOWN_RETIRING_ALL         0x00c2
#define UNC_M_CLOCKTICKS             0x0001
#define LONGEST_LAT_CACHE_MISS       0x412e // L3 cache misses
#define BR_MISP_RETIRED_ALL_BRANCHES 0x00c5 // branch misses
#define CSTATE_CORE_C6_RESIDENCY     0x0002 // FIXME this doesn't seem right according to perfmon-events.intel.com
#define CACHE_MISSES                 0x412E // Architectural counter

#define NUM_ENERGY_COUNTERS (1 + 4)

#define EFFICIENCY_CORE  0
#define PERFORMANCE_CORE 1

// The multiplicative factors given here are scaled up to allow for
// integer calculation. Scale these values down by 10^-12 to reach
// the proper model coefficients.
// The intercept values are given in pWs (That is, 10^-12 Ws or J).
// TODO use most recently determined coefficients
const s64 energy_model_factors[2][NUM_ENERGY_COUNTERS + 1] = {
	// last element in row is the intercept
	{ -40, 377, 212, 91, 1451, 5671202142645 - 2140000000000 }, // efficiency core
	{ 47, 788, -196120, 4182, 131261, -417594758113650 - 2125000000000 }, // performance core
};

struct measurement {
	u64 time;
	u64 counters[NUM_ENERGY_COUNTERS];
};

DEFINE_PER_CPU(struct measurement, pmlab_latest);
DEFINE_PER_CPU(unsigned, pmlab_core_type);

// Calling this function only makes sense if preemption is disabled.
static inline unsigned
my_core_type(void)
{
	return *this_cpu_ptr(&pmlab_core_type);
}

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

#if 0
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
#endif

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
	unsigned core_type = my_core_type();
	s64 energy_sum = energy_model_factors[core_type][NUM_ENERGY_COUNTERS];
	for (unsigned i = 0; i < NUM_ENERGY_COUNTERS; i++) {
		// Unsigned differentiation handles counter wrap-around
		u64 delta = end->counters[i] - start->counters[i];
		// Signed dot product allows for negative factors
		energy_sum += energy_model_factors[core_type][i] * (s64)delta;
	}
	// Total sum should not be negative, even if some products are
	return energy_sum < 0 ? 0 : (u64)energy_sum;
}

void
pmlab_reset_task_struct(struct task_struct *tsk)
{
	// Task structs are copied on fork().
	// We don't want our energy model to be copied,
	// so we have to reset its contents here.
	struct energy_model *em = &tsk->energy_model;
	memset(em, 0, sizeof *em);
	spin_lock_init(&em->lock);
}

void
pmlab_install_performance_counters(void)
{
	u64 proc_id = smp_processor_id();

	// Distinguish efficiency <-> performance cores
	uint32_t eax = 0x1A;
	__asm__ ("cpuid\n\t" : "+a"(eax) :: "ebx", "ecx", "edx"); // Get EAX of Leaf 0x1A
	unsigned core_family = eax >> 24;
	unsigned *core_type = &get_cpu_var(pmlab_core_type);
	if (core_family == 0x20) { // Intel Atom (Efficiency Core)
		*core_type = EFFICIENCY_CORE;
	} else { // Intel Core (Performance Core)
		*core_type = PERFORMANCE_CORE;
	}
	put_cpu_var(pmlab_core_type);

	printk("PMLab: Installing power performance counters on %c processor %llu.\n",
		my_core_type() == EFFICIENCY_CORE ? 'E' : 'P', proc_id);

	// Simply enable all fixed and programmable counters
	// FIXME this access gets trapped as illegal on E cores?
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0x70000003ful);

	// Configure the individual event counters
	if (my_core_type() == EFFICIENCY_CORE) {
		wrmsrl(MSR_ARCH_PERFMON_FIXED_CTR_CTRL, INTEL_FIXED_0_KERNEL | INTEL_FIXED_0_USER);
		wrmsrl(MSR_ARCH_PERFMON_EVENTSEL0, MEM_INST_RETIRED_ALL_LOADS | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
		wrmsrl(MSR_ARCH_PERFMON_EVENTSEL1, ICACHE_ACCESSES | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
		wrmsrl(MSR_ARCH_PERFMON_EVENTSEL2, TOPDOWN_RETIRING_ALL | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
		wrmsrl(MSR_ARCH_PERFMON_EVENTSEL3, UNC_M_CLOCKTICKS | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
	} else { // PERFORMANCE_CORE
		wrmsrl(MSR_ARCH_PERFMON_FIXED_CTR_CTRL, INTEL_FIXED_0_KERNEL | INTEL_FIXED_0_USER);
		wrmsrl(MSR_ARCH_PERFMON_EVENTSEL0, MEM_INST_RETIRED_ALL_LOADS | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
		wrmsrl(MSR_ARCH_PERFMON_EVENTSEL1, CACHE_MISSES | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
		wrmsrl(MSR_ARCH_PERFMON_EVENTSEL2, CSTATE_CORE_C6_RESIDENCY | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
		wrmsrl(MSR_ARCH_PERFMON_EVENTSEL3, UNC_M_CLOCKTICKS | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
	}

	// Gather a first measurement
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
