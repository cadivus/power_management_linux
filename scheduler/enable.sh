#!/bin/sh

# Usage: ./enable.sh

./build.sh $1

sudo ./disable.sh 

# Register the scheduler
sudo bpftool struct_ops register pm_lab_scheduler.bpf.c.o /sys/fs/bpf/sched_ext || (echo "Error attaching scheduler, consider calling disable.sh before" || exit 1)

# Print scheduler name, fails if it isn't registered properly
cat /sys/kernel/sched_ext/root/ops || (echo "No sched-ext scheduler installed" && exit 1)
