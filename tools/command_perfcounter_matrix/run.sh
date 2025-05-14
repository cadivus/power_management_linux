# Disable turbo mode
echo "1" | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo

# creates some files for monitoring
date >> start_date
date
./generate_command_perfcounter_matrix.py --commands commands --perf_counters perf_counters
date >> middle_date
echo ""
echo ""
echo ""
echo "half time"
date
./generate_command_perfcounter_matrix.py --commands commands --perf_counters perf_counters --little_cores
date >> end_date
date
