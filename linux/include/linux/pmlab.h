/* Power Management Lab */

#ifndef _KERNEL_POWER_MANAGEMENT_LAB_H
#define _KERNEL_POWER_MANAGEMENT_LAB_H

#include <linux/types.h>
#include <linux/spinlock_types.h>

struct task_struct; // defined externally

#define MAX_ENERGY_SAMPLES 256 // sizeof(energy_model->samples) == 4096

struct energy_sample {
	u64 energy;
	u64 duration;
};

struct energy_model {
	spinlock_t lock; // protects the entire struct

	u64 total_energy;
	u64 total_duration;

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

/* Returns the estimated power consumption of the given task in milliwatt.
 */
u64  pmlab_power_consumption_of_task(struct task_struct *tsk);

#endif
