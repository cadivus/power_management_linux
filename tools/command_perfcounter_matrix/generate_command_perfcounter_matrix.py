#!/usr/bin/env python3

import os
import sys
import csv
import argparse
import time
from datetime import datetime

# Add the helpers directory to sys.path to import run_perf_test
top_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.append(top_dir)
from helpers.perf_runner import run_perf_test


def read_file(filepath):
    """Read lines from a file and return as a list, stripping whitespace."""
    with open(filepath, 'r') as file:
        return [line.strip() for line in file.readlines() if line.strip()]


def save_to_csv(filename, header, data):
    """Save the results to a CSV file."""
    file_exists = os.path.isfile(filename)
    with open(filename, mode='a', newline='') as csv_file:
        writer = csv.writer(csv_file)
        if not file_exists:
            writer.writerow(header)
        writer.writerows(data)


def main():
    parser = argparse.ArgumentParser(description="Run performance tests and save results to CSV.")
    parser.add_argument("--commands", required=True, help="Path to the commands file.")
    parser.add_argument("--perf_counters", required=True, help="Path to the perf counters file.")
    parser.add_argument("--little_cores", action="store_true", help="Use little cores")
    args = parser.parse_args()

    # Read commands and performance counters
    commands = read_file(args.commands)
    perf_counters = read_file(args.perf_counters)

    # Generate CSV filename
    prefix = "LITTLE" if args.little_cores else "BIG"
    timestamp = datetime.utcnow().strftime("%Y-%m-%d__%H_%M_%S")
    csv_filename = f"{prefix}_{timestamp}.csv"

    # Prepare CSV header
    header = ["\\"]
    for counter in perf_counters:
        header.append(f"{counter}")
        header.append(f"{counter}-watt")
        header.append(f"{counter}-runtime")
    save_to_csv(csv_filename, header, [])

    # Process commands and collect data row by row
    for command in commands:
        time.sleep(5)  # Cooldown period before execution

        command_results = []
        for i in range(0, len(perf_counters), 4):  # Run in chunks of 4
            selected_counters = perf_counters[i:i + 4]

            time.sleep(2)  # Cooldown period before execution

            result = run_perf_test(command, selected_counters, args.little_cores)

            for counter in selected_counters:
                command_results.append(result.get(counter, 'N/A'))
                command_results.append(result.get('rapl_power_consumption_in_watt', 'N/A'))
                command_results.append(result.get(f"{counter}___runtime", 'N/A'))

        # Write results to CSV
        row = [command] + command_results
        save_to_csv(csv_filename, header, [row])
        print(f"Saved results for command: {command}")


if __name__ == "__main__":
    main()
