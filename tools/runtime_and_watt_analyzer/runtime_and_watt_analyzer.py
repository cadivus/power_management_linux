#!/usr/bin/env python3

import os
import sys
import argparse
import subprocess
import time
import csv
from datetime import datetime

# Add root directory to sys.path to access helpers
top_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.append(top_dir)

from helpers.get_cpu_topology import get_big_cores, get_little_cores, supports_big_little


def read_energy():
    with open("/sys/class/powercap/intel-rapl:0/energy_uj", "r") as f:
        return int(f.read().strip())


def run_command(command, little_cores=False):
    if little_cores and not supports_big_little():
        raise RuntimeError("Little cores not supported")

    cores = get_little_cores() if little_cores else get_big_cores()
    taskset_cmd = ["taskset", "-c", ",".join(map(str, cores))]
    full_cmd = " ".join(taskset_cmd + command.split())

    time.sleep(5)  # Cooldown period before execution

    start_energy = read_energy()
    start_time = time.time()

    result = subprocess.run(full_cmd, shell=True, capture_output=True, text=True)
    output = result.stderr

    end_energy = read_energy()
    end_time = time.time()

    consumption_joules = (end_energy - start_energy) / 1_000_000
    runtime_seconds = end_time - start_time
    average_power_watt = consumption_joules / runtime_seconds if runtime_seconds > 0 else float("nan")

    return runtime_seconds * 1000, average_power_watt


def main():
    parser = argparse.ArgumentParser(description="Run commands and measure runtime and power consumption.")
    parser.add_argument("input_file", type=str, help="Path to the input text file containing commands.")
    parser.add_argument("--little_cores", action="store_true", help="Use little cores")

    args = parser.parse_args()

    prefix = "LITTLE" if args.little_cores else "BIG"
    timestamp = datetime.utcnow().strftime("%Y-%m-%d__%H_%M_%S")
    csv_filename = f"{prefix}_{timestamp}.csv"

    with open(args.input_file, "r") as infile:
        csv_writer = csv.writer(open(csv_filename, "w", newline=''))
        csv_writer.writerow(["command", "runtime (ms)", "power consumption (W)"])

        for line in infile:
            command = line.strip()
            if command:
                try:
                    runtime, power = run_command(command, args.little_cores)
                    with open(csv_filename, "a", newline='') as outfile:
                        csv_writer = csv.writer(outfile)
                        csv_writer.writerow([command, runtime, power])
                    print(f"Executed: {command} | Runtime: {runtime:.2f} ms | Power: {power:.2f} W")
                except Exception as e:
                    print(f"Error running command '{command}': {e}")


if __name__ == "__main__":
    main()
