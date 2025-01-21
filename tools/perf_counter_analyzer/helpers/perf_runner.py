#!/usr/bin/env python3

import subprocess
import re
import time
try:
    from get_cpu_topology import get_big_cores, get_little_cores, supports_big_little
except ImportError:
    from .get_cpu_topology import get_big_cores, get_little_cores, supports_big_little


def read_energy():
    with open("/sys/class/powercap/intel-rapl:0/energy_uj", "r") as f:
        return int(f.read().strip())


def run_perf_test(command, perf_counters, little_cores=False):
    if little_cores and not supports_big_little():
        raise RuntimeError("Little cores not supported")

    cores = get_little_cores() if little_cores else get_big_cores()
    taskset_cmd = f"taskset -c {','.join(map(str, cores))}"
    perf_cmd = f"perf stat -e {','.join(perf_counters)} {command}"
    full_cmd = f"{taskset_cmd} {perf_cmd}"

    start_energy = read_energy()
    start_time = time.time()

    result = subprocess.run(full_cmd, shell=True, capture_output=True, text=True)
    output = result.stderr

    end_energy = read_energy()
    end_time = time.time()

    consumption_joules = (end_energy - start_energy) / 1_000_000
    runtime_seconds = end_time - start_time
    average_power_watt = consumption_joules / runtime_seconds if runtime_seconds > 0 else float("nan")

    perf_results = parse_perf_output(output, perf_counters, little_cores)
    perf_results = {**{'rapl_power_consumption_in_watt': round(average_power_watt, 2)}, **perf_results}

    return perf_results


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
                try:
                    if '<not counted>' in value or '<not supported>' in value:
                        data[counter] = float("nan")
                    else:
                        data[counter] = int(value.replace(',', '').replace('.', ''))
                except ValueError:
                    data[counter] = value.strip()

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
