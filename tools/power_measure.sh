#!/bin/bash

if [ -z "$1" ]; then
    echo "Usage: $0 <command>" >&2
    exit 1
fi

start_energy=$(cat /sys/class/powercap/intel-rapl:0/energy_uj)
start_time=$(date +%s.%N)

# Run the command passed as argument
"$@"

end_energy=$(cat /sys/class/powercap/intel-rapl:0/energy_uj)
end_time=$(date +%s.%N)

consumption=$(echo "scale=2; ($end_energy - $start_energy) / 1000000" | bc)
runtime=$(echo "$end_time - $start_time" | bc)
average_power=$(echo "scale=2; $consumption / $runtime" | bc)

echo "Energy Consumption: $consumption J" >&2
echo "Runtime: $runtime s" >&2
echo "Average Power: $average_power W" >&2
