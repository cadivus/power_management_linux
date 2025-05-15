# PM-Lab Group 3 scheduler

Our scheduler is implemented using the sched_ext scheduler class. Sched_ext enables us to use eBPF to enable, disable and modify our scheduler during runtime.
Also, a faulty scheduler is not as problematic compared to an implementation in the kernel itself, because the sched_ext scheduler in the kernel will disable our scheduler if it misbehaves.

The starting point for this work and source of the setup/build/enbale/disable script is the "Minimal scheduler" examples (https://github.com/parttimenerd/minimal-scheduler) from Johannes Bechberger.
