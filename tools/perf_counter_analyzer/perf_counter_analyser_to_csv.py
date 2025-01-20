#!/usr/bin/env python3

import csv
import os
import sys
import argparse
from datetime import datetime
from helpers.get_perf_counters import get_perf_counters
from helpers.perf_runner import run_perf_test


def main(little_cores):
    # Get performance counters
    perf_counters_map = get_perf_counters()
    all_perf_counters = sum(perf_counters_map.values(), [])  # Concatenate all lists to one list

    # Prepare CSV file
    prefix = "LITTLE" if little_cores else "BIG"
    timestamp = datetime.utcnow().strftime("%Y-%m-%d__%H_%M_%S")
    csv_filename = f"{prefix}_{timestamp}.csv"
    csv_headers = [
        "performance_counter", "run", "count_idle", "watt_idle",
        "count_load", "watt_load", "count_multiplier", "watt_multiplier"
    ]

    with open(csv_filename, mode='w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=csv_headers)
        writer.writeheader()

    # Iterate through performance counters in chunks of 4
    for i in range(0, len(all_perf_counters), 4):
        batch_counters = all_perf_counters[i:i + 4]
        print(f"Running iteration {i // 4 + 1} / {len(all_perf_counters) // 4 + (1 if len(all_perf_counters) % 4 != 0 else 0)}")

        # Run idle test
        command_idle = "sleep 10s"
        result1 = run_perf_test(command_idle, batch_counters, little_cores)

        # Run load test
        command_load = "stress-ng --cpu 2 --cpu-method prime --timeout 10s"
        result2 = run_perf_test(command_load, batch_counters, little_cores)

        with open(csv_filename, mode='a', newline='') as csvfile:
            writer = csv.DictWriter(csvfile, fieldnames=csv_headers)
            for counter in batch_counters:
                row = {
                    "performance_counter": counter,
                    "run": i // 4 + 1,
                    "count_idle": f"{result1[counter]}",
                    "watt_idle": f"{result1['rapl_power_consumption_in_watt']}",
                    "count_load": f"{result2[counter]}",
                    "watt_load": f"{result2['rapl_power_consumption_in_watt']}",
                    "count_multiplier": f"{result2[counter] / result1[counter] if result1[counter] != 0 else 0}",
                    "watt_multiplier": f"{result2['rapl_power_consumption_in_watt'] / result1['rapl_power_consumption_in_watt'] if result1['rapl_power_consumption_in_watt'] != 0 else 0}"
                }
                writer.writerow(row)

    print(f"Results saved to {csv_filename}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run performance tests with perf counters.")
    parser.add_argument("--little_cores", action="store_true", help="Use little cores")
    args = parser.parse_args()
    main(args.little_cores)
