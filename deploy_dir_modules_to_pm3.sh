#!/bin/bash

echo "Checking CPU utilization on pm3 (other builds)..."
if ! ssh -J pm3@lab.os.itec.kit.edu pm3@pm3 "mpstat 1 3 | tail -n 1 | awk '{if (100 - \$12 > 10) exit 1; else exit 0;}'"; then
    echo "Error: CPU utilization is above 10% on the remote machine, so there is probably a build running. Please try again later."
    exit 1
fi

set -e

start_time=$(date +%s)

# Will be the path of this script
scriptdir=$(cd $(dirname $0); pwd -P)

build_start_time=$(date +%s)
rsync -avz --delete-during -e "ssh -J pm3@lab.os.itec.kit.edu" "$scriptdir/" pm3@pm3:/unser_zeug/git/deploy_script/repo/power_management_linux

# the building is missing, but the module files get copied

end_time=$(date +%s)
runtime=$((end_time - start_time))
minutes=$((runtime / 60))
seconds=$((runtime % 60))
echo "Script runtime: ${minutes} minutes and ${seconds} seconds"
