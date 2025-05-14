#!/usr/bin/env python3

import re
import subprocess
import json


def get_perf_counters():
    """
    Runs `perf list` in a subprocess, collects its output,
    and parses it into a dictionary of {group_name: [list_of_events]}.
    """
    perf_output = subprocess.check_output(["perf", "list"], text=True, stderr=subprocess.DEVNULL)
    lines = perf_output.splitlines()

    # Regex to match a group heading of the form:
    #   something:        (optionally with bracketed text after)
    # Examples:
    #   cpu_atom:
    #   Backend: [some text]
    group_heading_regex = re.compile(r'^(\S+):(\s*\[.*\])?\s*$')

    groups = {}
    current_group = None

    for raw_line in lines:
        line = raw_line.rstrip('\n')

        # Count leading spaces
        # If more than two leading spaces, ignore the line (assumed description)
        leading_spaces = len(line) - len(line.lstrip(' '))
        if leading_spaces > 2:
            continue

        # Check if this line defines a new group heading
        heading_match = group_heading_regex.match(line.strip())
        if heading_match:
            # We found a new group heading
            current_group = heading_match.group(1)
            # Initialize that group in our dictionary
            groups[current_group] = []
            continue

        # If we are not in a group, ignore
        if not current_group:
            continue

        # Strip leading/trailing whitespace
        stripped = line.strip()

        # If it's empty after stripping, ignore
        if not stripped:
            continue

        # If it starts with '[', likely a bracketed description => ignore
        if stripped.startswith('['):
            continue

        # Remove any trailing bracketed part, e.g. "[Hardware event]"
        bracket_pos = stripped.find('[')
        if bracket_pos != -1:
            stripped = stripped[:bracket_pos].rstrip()

        # Now handle the " OR " part; we only want the first token
        or_pos = stripped.find(' OR ')
        if or_pos != -1:
            stripped = stripped[:or_pos].rstrip()

        # If there's still something left, add it as an event
        if stripped:
            groups[current_group].append(stripped)

    # Remove any groups that ended up empty
    groups = {g: evts for g, evts in groups.items() if evts}

    return groups


if __name__ == "__main__":
    result = get_perf_counters()
    # Print as JSON
    print(json.dumps(result, indent=2))
