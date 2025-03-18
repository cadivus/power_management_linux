#!/usr/bin/env python3
import csv
import sys
import os

def parse_number(val):
    """
    Try to parse a numeric value from a string.
    If the value ends with " ns", remove it and note the suffix.
    Returns a tuple: (numeric_value or None, suffix flag as bool, original string).
    """
    original = val.strip()
    suffix = ""
    if original.endswith(" ns"):
        suffix = " ns"
        original = original[:-3].strip()
    try:
        num = float(original)
        return num, suffix, val.strip()
    except Exception:
        return None, "", val.strip()

def compute_normalized(numer_str, runtime_str):
    """
    Compute the division: (numer / runtime). If either conversion fails or division by zero occurs,
    return the original numer_str unchanged.
    If numer_str ends with " ns", append " ns" to the result.
    """
    num, ns_flag, _ = parse_number(numer_str)
    denom, _, _ = parse_number(runtime_str)
    if num is None or denom is None or denom == 0:
        return numer_str  # keep original on error
    result = num / denom
    # If the original numerator had a " ns" suffix, add it back.
    suffix = " ns" if numer_str.strip().endswith(" ns") else ""
    return str(result) + suffix

def compute_median(values):
    """
    Compute the median of a list of floats.
    For an even count, the median is the average of the two middle values.
    """
    sorted_vals = sorted(values)
    n = len(sorted_vals)
    if n == 0:
        return None
    if n % 2 == 1:
        return sorted_vals[n // 2]
    else:
        return (sorted_vals[n // 2 - 1] + sorted_vals[n // 2]) / 2

def normalize_csv(input_file):
    # Determine output filename:
    base, ext = os.path.splitext(input_file)
    if ext.lower() == ".csv":
        output_file = f"{base}-normalized{ext}"
    else:
        output_file = f"{input_file}-normalized"
    
    with open(input_file, newline='', encoding='utf-8') as inf:
        reader = csv.reader(inf)
        rows = list(reader)
    if not rows:
        print("Empty CSV file.")
        return

    # Assume the first row is header.
    header = rows[0]
    # The first column (index 0) is the identifier.
    # The rest of the columns come in groups of three: name, name-watt, name-runtime.
    groups = []
    i = 1
    while i < len(header):
        # Check if there are enough columns for a group.
        if i + 2 < len(header):
            group_name = header[i]
            watt_header = header[i+1]
            runtime_header = header[i+2]
            # Optionally, you can check that watt_header endswith "-watt" and runtime_header endswith "-runtime"
            groups.append((group_name, watt_header, runtime_header))
            i += 3
        else:
            # If columns are not a multiple of 3, break out.
            break

    # Build new header: first column unchanged, then "watt-median", then one column per group (using group name)
    new_header = [header[0], "watt-median"] + [grp[0] for grp in groups]

    new_rows = [new_header]

    # Process each row.
    for row in rows[1:]:
        # Copy the identifier column.
        new_row = [row[0]]
        watt_values = []
        watt_ns_flag = False

        # For each group, try to extract the watt value.
        for idx, group in enumerate(groups):
            # Calculate column indices: group columns start at 1 + idx*3
            watt_col_idx = 1 + idx*3 + 1  # second column in the group
            if watt_col_idx < len(row):
                val = row[watt_col_idx].strip()
                num, ns, _ = parse_number(val)
                if num is not None:
                    watt_values.append(num)
                    if ns:
                        watt_ns_flag = True
        # Compute median for watt values.
        if watt_values:
            median_val = compute_median(watt_values)
            watt_median_str = str(median_val)
            if watt_ns_flag:
                watt_median_str += " ns"
        else:
            watt_median_str = ""
        new_row.append(watt_median_str)

        # For each group, compute normalized value: (name / name-runtime)
        for idx, group in enumerate(groups):
            # Numerator is the first column of the group.
            numer_idx = 1 + idx*3
            runtime_idx = 1 + idx*3 + 2
            if numer_idx < len(row) and runtime_idx < len(row):
                numer_val = row[numer_idx].strip()
                runtime_val = row[runtime_idx].strip()
                normalized = compute_normalized(numer_val, runtime_val)
                new_row.append(normalized)
            else:
                new_row.append("")
        new_rows.append(new_row)

    # Write out the new CSV.
    with open(output_file, "w", newline='', encoding='utf-8') as outf:
        writer = csv.writer(outf)
        writer.writerows(new_rows)
    print(f"Normalized CSV written to {output_file}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: {} <input_csv>".format(sys.argv[0]))
        sys.exit(1)
    input_csv = sys.argv[1]
    normalize_csv(input_csv)

