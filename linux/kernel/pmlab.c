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
#define IA32_PERF_GLOBAL_INUSE 0x392

// Perfmon event numbers (low byte) with umasks (high byte).
// Different CPU types may use different event numbers for the same event.
// Perf cores are CORE type, efficiency cores are ATOM type.
#define ATOM_L1_dcache_loads  0x81d0 // source: https://community.intel.com/t5/Software-Tuning-Performance/Understanding-L1-L2-L3-misses/m-p/1056573
#define ATOM_L1_icache_loads  0x0380 // Not sure. We use ICACHE.ACCESSES here.
#define ATOM_iTLB_load_misses 0x0481 // ITLB.MISS
#define ATOM_bus_cycles       0x013c
#define CORE_dTLB_loads       0x81d0 // source: https://stackoverflow.com/questions/56172115/what-is-the-meaning-of-perf-events-dtlb-loads-and-dtlb-stores
#define CORE_bus_cycles       0x013c
#define CORE_mem_stores       0x02cd
#define CORE_ref_cycles       0x013c

#define EFFICIENCY_CORE  0
#define PERFORMANCE_CORE 1

const unsigned energy_model_events[2][NUM_ENERGY_COUNTERS] = {
	{ 0, ATOM_L1_dcache_loads, ATOM_L1_icache_loads, ATOM_iTLB_load_misses, ATOM_bus_cycles }, // efficiency core
	{ 0, CORE_dTLB_loads, CORE_bus_cycles, CORE_mem_stores, CORE_ref_cycles }, // performance core
};

// The multiplicative factors given here are scaled up to allow for
// integer calculation. Scale these values down by 10^-12 to reach
// the proper model coefficients.
// The intercept values are given in pWs (That is, 10^-12 Ws or J).
// TODO use most recently determined coefficients
const s64 energy_model_coefficients[2][NUM_ENERGY_COUNTERS + 1] = {
	// last element in row is the intercept
	{ 37, 321, 178, 2910827, 1026, 3160539047230 }, // efficiency core
	{ 65, 904, 15064, 0, 0, -10925121778307 }, // performance core
};

const int energy_model_squared[2][NUM_ENERGY_COUNTERS] = {
	{ 0, 0, 0, 0, 0 }, // efficiency core
	{ 0, 0, 0, 1, 1 }, // performance core
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

#if 0
static inline u64
precise_mul_div(u64 low, u64 high, u64 mult, u64 quot)
{
	__asm__ (
		"mulq %2\n\t"
		"divq %3"
		: "+a"(low), "+d"(high)
		: "rm"(mult), "rm"(quot)
		: "cc");
	return low;
}
#endif

// Calling this function only makes sense if preemption is disabled.
static inline int
my_core_type(void)
{
	return *this_cpu_ptr(&pmlab_core_type);
}

static void
gather_energy_counts(struct energy_counts *ec)
{
	ec->duration = ktime_get_ns();
	rdmsrl(MSR_ARCH_PERFMON_FIXED_CTR0, ec->counters[0]);
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR0,   ec->counters[1]);
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR1,   ec->counters[2]);
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR2,   ec->counters[3]);
	rdmsrl(MSR_ARCH_PERFMON_PERFCTR3,   ec->counters[4]);
}

static void
accumulate_energy_counts(struct energy_counts *accum, const struct energy_counts *start, const struct energy_counts *end, pid_t pid)
{
	// Compute elapsed time + increases in counters.
	// Unsigned differentiation handles counter wrap-around.
	struct energy_counts delta;
	delta.duration = end->duration - start->duration;
	for (unsigned i = 0; i < NUM_ENERGY_COUNTERS; i++) {
		delta.counters[i] = end->counters[i] - start->counters[i];
	}

	// Look out for unsigned integer overflows
	int overflowing = 0;
	overflowing |= (accum->duration + delta.duration < accum->duration);
	for (unsigned i = 0; i < NUM_ENERGY_COUNTERS; i++) {
		overflowing |= (accum->counters[i] + delta.counters[i] < accum->counters[i]);
	}

	if (overflowing) {
		// If we would overflow, scale everything down proportionally.
		// This effectively serves as a kind of slow rolling average as well.
		printk("PMLab: scaling down event counts for pid %llu, as they grew too large.\n", (long long unsigned)pid);
		const unsigned down_shift = 4;
		accum->duration >>= down_shift;
		for (unsigned i = 0; i < NUM_ENERGY_COUNTERS; i++) {
			accum->counters[i] >>= down_shift;
		}
	}

	// Add to running sum
	accum->duration += delta.duration;
	for (unsigned i = 0; i < NUM_ENERGY_COUNTERS; i++) {
		accum->counters[i] += delta.counters[i];
	}
}

static u64
evaluate_energy_model(const struct energy_counts *ec, int core_type)
{
	// x86-64 can easily do 128-bit arithmatic.
	// We use this extra precision here to avoid overflows
	// from multiplications.
	s64 energy[2] = { 0, 0 };
	for (unsigned i = 0; i < NUM_ENERGY_COUNTERS; i++) {
		u64 coeff = energy_model_coefficients[core_type][i];
		s64 magnitude = (s64)ec->counters[i];
		if (energy_model_squared[core_type][i]) {
			magnitude *= magnitude;
		}

		s64 contrib[2] = { 0, 0 };
		mul_s64_s128(contrib, coeff, magnitude);

		add_s128(energy, contrib);
	}

	u64 energy_mJ = div_s128_s64(energy, 1000000000l);
	// While individual contributions may be negative,
	// the final estimate should not be.
	return energy_mJ >= 0 ? energy_mJ : 0;
}

void
pmlab_reset_task_struct(struct task_struct *tsk)
{
	// Task structs are copied on fork().
	// We don't want our energy model to be copied,
	// so we have to reset its contents here.
	struct energy_model *em = &tsk->energy_model;
	memset(em, 0, sizeof *em);
	// Doesn't matter if this guess is wrong,
	// since this will get updated after the first timeslice.
	em->core_type = EFFICIENCY_CORE;
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
	int *core_type_ptr = &get_cpu_var(pmlab_core_type);
	if (core_family == 0x20) { // Intel Atom (Efficiency Core)
		*core_type_ptr = EFFICIENCY_CORE;
	} else { // Intel Core (Performance Core)
		*core_type_ptr = PERFORMANCE_CORE;
	}
	put_cpu_var(pmlab_core_type);

	int core_type = my_core_type();

	printk("PMLab: Installing power performance counters on %c processor %llu.\n",
		core_type == EFFICIENCY_CORE ? 'E' : 'P', proc_id);

	// Simply enable all fixed and programmable counters
	// FIXME this access gets trapped as illegal on E cores?
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, 0x70000003ful);

	// Configure the individual event counters
	wrmsrl(MSR_ARCH_PERFMON_FIXED_CTR_CTRL, INTEL_FIXED_0_USER); // instructions-retired
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL0, energy_model_events[core_type][1] | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL1, energy_model_events[core_type][2] | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL2, energy_model_events[core_type][3] | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);
	wrmsrl(MSR_ARCH_PERFMON_EVENTSEL3, energy_model_events[core_type][4] | ARCH_PERFMON_EVENTSEL_USR | ARCH_PERFMON_EVENTSEL_ENABLE);

	const u64 my_counters_mask = 0x000000010000000ful;
	u64 others_counters_mask;
	rdmsrl(IA32_PERF_GLOBAL_INUSE, others_counters_mask);
	if (my_counters_mask & others_counters_mask) {
		printk("PMLab: core %llu: clashing PMC counter usage: we need %llx, others use %llx.", proc_id, my_counters_mask, others_counters_mask);
	}
	wrmsrl(IA32_PERF_GLOBAL_INUSE, my_counters_mask);

	// Gather a first measurement on this CPU core
	struct energy_counts *latest = &get_cpu_var(pmlab_previous_counts);
	gather_energy_counts(latest);
	put_cpu_var(pmlab_previous_counts);
}

void
pmlab_update_after_timeslice(struct task_struct *prev)
{
	struct energy_counts *latest = &get_cpu_var(pmlab_previous_counts);
	struct energy_counts end, start = *latest;
	gather_energy_counts(&end);
	*latest = end;
	put_cpu_var(pmlab_previous_counts);

	struct energy_model *em = &prev->energy_model;
	unsigned long cpu_flags;
	spin_lock_irqsave(&em->lock, cpu_flags);

	// If the task has been rescheduled to a different kind of core,
	// throw out the data we have collected, since it is core-type specific.
	int core_type = my_core_type();
	if (em->core_type != core_type) {
		memset(&em->counts, 0, sizeof em->counts);
		em->core_type = core_type;
	}

	accumulate_energy_counts(&em->counts, &start, &end, prev->pid);

	spin_unlock_irqrestore(&em->lock, cpu_flags);
}

u64
pmlab_power_consumption_of_task(struct task_struct *tsk)
{
	struct energy_model *em = &tsk->energy_model;
	unsigned long cpu_flags;
	spin_lock_irqsave(&em->lock, cpu_flags);

	struct energy_counts counts = em->counts;
	int core_type = em->core_type;

	spin_unlock_irqrestore(&em->lock, cpu_flags);

	u64 energy_mJ = evaluate_energy_model(&counts, core_type);

	// TODO return power instead of energy
	return energy_mJ;
}

#if 0
#define MEM_INST_RETIRED_ALL_LOADS   0x81d0 // maps to dTLB-loads
#define ICACHE_ACCESSES              0x0380
#define TOPDOWN_RETIRING_ALL         0x00c2
#define UNC_M_CLOCKTICKS             0x0001
#define LONGEST_LAT_CACHE_MISS       0x412e // L3 cache misses
#define BR_MISP_RETIRED_ALL_BRANCHES 0x00c5 // branch misses
#define CSTATE_CORE_C6_RESIDENCY     0x0002 // FIXME this doesn't seem right according to perfmon-events.intel.com
#define CACHE_MISSES                 0x412E // Architectural counter
#endif
