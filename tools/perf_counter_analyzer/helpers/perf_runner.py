#!/usr/bin/env python3

import subprocess
import re
from get_cpu_topology import get_big_cores, get_little_cores, supports_big_little


def run_perf_test(command, perf_counters, little_cores=False):
    if little_cores and not supports_big_little():
        raise RuntimeError("Little cores not supported")

    cores = get_little_cores() if little_cores else get_big_cores()
    taskset_cmd = f"taskset -c {','.join(map(str, cores))}"
    perf_cmd = f"perf stat -e {','.join(perf_counters)} {command}"
    full_cmd = f"{taskset_cmd} {perf_cmd}"

    result = subprocess.run(full_cmd, shell=True, capture_output=True, text=True)
    output = result.stderr

    return parse_perf_output(output, perf_counters, little_cores)


def parse_perf_output(output, perf_counters, little_cores):
    data = {}
    lines = output.splitlines()
    parsing = False

    for line in lines:
        if line.startswith(" Performance counter stats for"):
            parsing = True
            continue

        if not parsing:
            continue

        match = re.match(r"\s*([0-9.,<not counted><not supported>]*)\s+([\w/_-]+)", line)
        if match:
            value, counter = match.groups()
            if little_cores:
                counter = re.sub(r"cpu_atom/|:u/", "", counter)
            else:
                counter = re.sub(r"cpu_core/|:u/", "", counter)
            if counter in perf_counters:
                if '<not counted>' in value or '<not supported>' in value:
                    data[counter] = float("nan")
                else:
                    data[counter] = int(value.replace(',', '').replace('.', ''))

    for counter in perf_counters:
        if counter not in data:
            data[counter] = float("nan")

    return data


if __name__ == "__main__":
    perf_counters = [
        "L1-dcache-loads",
        "L1-dcache-stores",
        "L1-icache-loads",
        "L1-icache-load-misses"
    ]

    print("Running sleep 5s with big cores")
    result1 = run_perf_test("sleep 5s", perf_counters, little_cores=False)
    print(result1)

    print("\nRunning stress-ng with big cores")
    result2 = run_perf_test("stress-ng --cpu 2 --cpu-method prime --timeout 5s", perf_counters, little_cores=False)
    print(result2)

    print("")
    print("")
    print("")

    print("Running sleep 5s with little cores")
    result3 = run_perf_test("sleep 5s", perf_counters, little_cores=True)
    print(result3)

    print("\nRunning stress-ng with little cores")
    result4 = run_perf_test("stress-ng --cpu 2 --cpu-method prime --timeout 5s", perf_counters, little_cores=True)
    print(result4)
