/* Power Management Lab */

#ifndef _KERNEL_POWER_MANAGEMENT_LAB_H
#define _KERNEL_POWER_MANAGEMENT_LAB_H

#include <linux/types.h>
#include <linux/spinlock_types.h>

struct task_struct; // defined externally

#define MAX_ENERGY_SAMPLES 16

struct energy_sample {
	u64 scaled_value;
	//u64 time_end;
	//u64 time_delta;
	//u64 ctr_deltas[5];
};

struct energy_model {
	spinlock_t lock; // protects the entire struct
	u64        power;

	/* ring buffer of samples */
	struct energy_sample samples[MAX_ENERGY_SAMPLES];
	int                  first_sample;
	int                  num_samples;
};

 /* task_struct contents are copied on fork.
  * Since we want independent measurements for all tasks,
  * we have to re-initialize our counters with 0 during fork.
  */
void pmlab_reset_task_struct(struct task_struct *tsk);

/* Tell the CPU core to enable perf counters, and which ones to track.
 * This function must not only be called on startup, but also subsequent wake-ups
 * (cpu_startup_entry() should take care of this).
 *
 * This function is called from cpu_startup_entry() in sched/idle.c
 */
void pmlab_install_performance_counters(void);

/* Determine event counter delta over time slice and update accordingly.
 *
 * This function is called from context_switch() in sched/core.c
 */
void pmlab_update_after_timeslice(struct task_struct *prev);

/* Returns the estimated power consumption of the given task.
 */
u64  pmlab_power_consumption_of_task(struct task_struct *tsk);

#endif
