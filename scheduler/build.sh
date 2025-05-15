#!/bin/sh


# Create the vmlinux header with all the eBPF Linux functions
# if it doesn'r exist
if [ ! -f vmlinux.h ]; then
    echo "Creating vmlinux.h"
    bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
fi

# Compile the scheduler
clang -target bpf -g -O2 -c pm_lab_scheduler.bpf.c -o pm_lab_scheduler.bpf.c.o -I.
