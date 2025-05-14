#!/usr/bin/env python3

import subprocess
import re


def parse_lscpu_output():
    result = subprocess.run(['lscpu', '-e'], capture_output=True, text=True)
    lines = [line for line in result.stdout.strip().split('\n') if line.strip()]

    # Skip the header row
    data = []
    for line in lines[1:]:
        parts = re.split(r'\s+', line.strip())
        if not parts[0].isdigit():
            continue  # Skip lines that do not start with a digit
        if parts[-1] == 'no':
            continue  # Ignore offline CPUs

        core_id = int(parts[0])
        max_mhz = None
        if 'MAXMHZ' in lines[0]:  # Check if MAXMHZ column exists
            max_mhz = float(parts[-3].replace(',', '').replace('.', ''))
        data.append((core_id, max_mhz))

    return data


def get_big_cores():
    cores = parse_lscpu_output()

    # If no frequency data, all cores are big
    if all(core[1] is None for core in cores) or len(set(core[1] for core in cores if core[1] is not None)) == 1:
        return sorted(set(core[0] for core in cores))

    max_freq = max(core[1] for core in cores if core[1] is not None)
    big_cores = sorted(set(core[0] for core in cores if core[1] == max_freq))
    return big_cores


def get_little_cores():
    cores = parse_lscpu_output()

    if all(core[1] is None for core in cores) or len(set(core[1] for core in cores if core[1] is not None)) == 1:
        return []  # No little cores if all frequencies are the same

    min_freq = min(core[1] for core in cores if core[1] is not None)
    little_cores = sorted(set(core[0] for core in cores if core[1] == min_freq))
    return little_cores


def supports_big_little():
    return bool(get_little_cores())


# Example usage
if __name__ == "__main__":
    print("Big cores:", get_big_cores())
    print("Little cores:", get_little_cores())
    print("Supports big.LITTLE:", supports_big_little())
