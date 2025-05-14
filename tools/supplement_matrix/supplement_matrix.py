#!/usr/bin/env python3
import pandas as pd
import math
import sys
import os

def process_value(val):
    """
    Process a cell value:
      - If it ends with ' ns', remove that, parse the number,
        compute its square root and square, and append ' ns' again.
      - If the value can't be parsed as a float, return the original value for both calculations.
    """
    suffix = " ns"
    is_ns = False

    # Convert the value to a string for suffix checking.
    s = str(val).strip()
    if s.endswith(suffix):
        is_ns = True
        s = s[:-len(suffix)].strip()
    
    try:
        num = float(s)
        root = math.sqrt(num)
        square = num * num
        if is_ns:
            return f"{root} ns", f"{square} ns"
        else:
            return root, square
    except ValueError:
        # If parsing fails, simply return the original value for both new columns.
        return val, val

def get_output_path(input_path):
    """
    Generates the output file path by inserting '-supplemented'
    before the file extension, or at the end of the file name if no extension is found.
    """
    base, ext = os.path.splitext(input_path)
    return f"{base}-supplemented{ext}" if ext else f"{input_path}-supplemented"

def main():
    if len(sys.argv) < 2:
        print("Usage: python script.py input.csv")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = get_output_path(input_file)
    
    # Read the CSV file.
    df = pd.read_csv(input_file)
    
    # List to hold the new column order.
    new_columns = []
    # Dictionary to hold data for the new DataFrame.
    new_data = {}

    # Keep the first two columns unchanged.
    for col in df.columns[:2]:
        new_columns.append(col)
        new_data[col] = df[col]
    
    # Process the rest of the columns.
    for col in df.columns[2:]:
        # Add the original column.
        new_columns.append(col)
        new_data[col] = df[col]
        
        # Define new column names for root and square.
        root_col = col + "-root"
        square_col = col + "-square"
        
        # Process the column values.
        processed = df[col].apply(lambda x: process_value(x))
        new_data[root_col] = processed.apply(lambda t: t[0])
        new_data[square_col] = processed.apply(lambda t: t[1])
        
        # Insert the new column names immediately after the original.
        new_columns.append(root_col)
        new_columns.append(square_col)
    
    # Create a new DataFrame with the specified column order.
    new_df = pd.DataFrame(new_data)[new_columns]
    new_df.to_csv(output_file, index=False)
    print(f"Processed CSV saved to {output_file}")

if __name__ == "__main__":
    main()

