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
    all_perf_counters = list(dict.fromkeys(all_perf_counters))  # Remove duplicates

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
        print(
            f"Running iteration {i // 4 + 1} / {len(all_perf_counters) // 4 + (1 if len(all_perf_counters) % 4 != 0 else 0)}")

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
                    "count_idle": result1[counter],
                    "watt_idle": f"{result1['rapl_power_consumption_in_watt']}",
                    "count_load": result2[counter],
                    "watt_load": f"{result2['rapl_power_consumption_in_watt']}",
                    "count_multiplier": "nan",
                    "watt_multiplier": f"{result2['rapl_power_consumption_in_watt'] / result1['rapl_power_consumption_in_watt'] if result1['rapl_power_consumption_in_watt'] != 0 else 0}"
                }

                if isinstance(result1[counter], (str, bytes)) and isinstance(result2[counter], (str, bytes)):
                    # Check if it's just two numbers with the same unit
                    parts1 = result1[counter].split()
                    parts2 = result2[counter].split()
                    if len(parts1) == 2 and len(parts2) == 2 and parts1[1] == parts2[1]:
                        try:
                            row["count_multiplier"] = f"{int(parts2[0].replace(',', '').replace('.', '')) / int(parts1[0].replace(',', '').replace('.', '')) if int(parts1[0].replace(',', '').replace('.', '')) != 0 else 0}"
                        except ValueError:
                            pass
                elif (not isinstance(result1[counter], (str, bytes))) and (not isinstance(result2[counter], (str, bytes))):
                    try:
                        row["count_idle"] = f"{result1[counter]}"
                        row["count_load"] = f"{result2[counter]}"
                        row["count_multiplier"] = f"{result2[counter] / result1[counter] if result1[counter] != 0 else 0}"
                    except Exception:
                        pass

                writer.writerow(row)

    print(f"Results saved to {csv_filename}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run performance tests with perf counters.")
    parser.add_argument("--little_cores", action="store_true", help="Use little cores")
    args = parser.parse_args()
    main(args.little_cores)
