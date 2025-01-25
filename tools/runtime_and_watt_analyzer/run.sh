# Disable turbo mode
echo "1" | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo

# creates some files for monitoring
date >> start_date
date
./runtime_and_watt_analyzer.py stressNgCommandList
date >> middle_date
echo ""
echo ""
echo ""
echo "half time"
date
./runtime_and_watt_analyzer.py --little_cores stressNgCommandList
date >> end_date
date
