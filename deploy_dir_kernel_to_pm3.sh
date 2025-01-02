#!/usr/bin/env bash

echo "Checking CPU utilization on pm3 (other builds)..."
if ! ssh -J pm3@lab.os.itec.kit.edu pm3@pm3 "mpstat 1 3 | tail -n 1 | awk '{if (100 - \$12 > 10) exit 1; else exit 0;}'"; then
    echo "Error: CPU utilization is above 10% on the remote machine, so there is probably a build running. Please try again later."
    exit 1
fi

set -e

start_time=$(date +%s)

# Will be the path of this script
scriptdir=$(cd $(dirname $0); pwd -P)

githash=$(git rev-parse --short HEAD)
if ! git diff --quiet || ! git diff --cached --quiet; then
    # Add -dirty if some changes are not commited
    githash="${githash}-dirty"
fi

build_start_time=$(date +%s)
rsync -avz --delete-during -e "ssh -J pm3@lab.os.itec.kit.edu" "$scriptdir/" pm3@pm3:/unser_zeug/git/deploy_script/repo/power_management_linux
echo "Build on pm3:"
ssh -J pm3@lab.os.itec.kit.edu pm3@pm3 "cd /unser_zeug/git/deploy_script/repo/power_management_linux/linux && make -j20 LOCALVERSION=-$githash"
end_time=$(date +%s)
runtime=$((end_time - build_start_time))
minutes=$((runtime / 60))
seconds=$((runtime % 60))
echo "Build runtime: ${minutes} minutes and ${seconds} seconds"

echo ""
echo "Install modules on pm3 (root password will be autotyped):"
ssh -J pm3@lab.os.itec.kit.edu pm3@pm3 "bash -c 'cd /unser_zeug/git/deploy_script/repo/power_management_linux/linux && sudo -S <<< \"pm3\" make modules_install -j20 LOCALVERSION=-$githash'"
echo ""
echo "Install kernel on pm3 (root password will be autotyped):"
ssh -J pm3@lab.os.itec.kit.edu pm3@pm3 "bash -c 'cd /unser_zeug/git/deploy_script/repo/power_management_linux/linux && sudo -S <<< \"pm3\" make install -j20 LOCALVERSION=-$githash'"
echo "Done, you can now reboot the machine."

end_time=$(date +%s)
runtime=$((end_time - start_time))
minutes=$((runtime / 60))
seconds=$((runtime % 60))
echo "Script runtime: ${minutes} minutes and ${seconds} seconds"
