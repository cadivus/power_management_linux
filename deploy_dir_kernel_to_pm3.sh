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

# For always booting the latest.
buildtime=$(printf "%06d" $(( ($(date +%s) - $(date -d '2024-12-17 10:00:00' +%s)) / 60 / 10 )))

githash=$(git rev-parse --short HEAD)
if ! git diff --quiet || ! git diff --cached --quiet; then
    # Add -dirty if some changes are not commited
    githash="${githash}-dirty"
fi

build_start_time=$(date +%s)
rsync -avz --delete-during -e "ssh -J pm3@lab.os.itec.kit.edu" "$scriptdir/" pm3@pm3:/unser_zeug/git/deploy_script/repo/power_management_linux
echo "Build on pm3:"
ssh -J pm3@lab.os.itec.kit.edu pm3@pm3 "cd /unser_zeug/git/deploy_script/repo/power_management_linux/linux && make -j20 LOCALVERSION=-$buildtime-$githash"
end_time=$(date +%s)
runtime=$((end_time - build_start_time))
minutes=$((runtime / 60))
seconds=$((runtime % 60))
echo "Build runtime: ${minutes} minutes and ${seconds} seconds"

echo ""
echo "Install modules on pm3 (root password will be autotyped):"
ssh -J pm3@lab.os.itec.kit.edu pm3@pm3 "bash -c 'cd /unser_zeug/git/deploy_script/repo/power_management_linux/linux && sudo -S <<< \"pm3\" make modules_install -j20 LOCALVERSION=-$buildtime-$githash'"
echo ""
echo "Install kernel on pm3 (root password will be autotyped):"
ssh -J pm3@lab.os.itec.kit.edu pm3@pm3 "bash -c 'cd /unser_zeug/git/deploy_script/repo/power_management_linux/linux && sudo -S <<< \"pm3\" make install -j20 LOCALVERSION=-$buildtime-$githash'"

echo "Copy stuff we need to build modules"
rsync -avz --delete-during -e "ssh -J pm3@lab.os.itec.kit.edu" pm3@pm3:/unser_zeug/git/deploy_script/repo/power_management_linux/linux/arch/x86/include/generated/ "$scriptdir/linux/arch/x86/include/generated"
rsync -avz --delete-during -e "ssh -J pm3@lab.os.itec.kit.edu" pm3@pm3:/unser_zeug/git/deploy_script/repo/power_management_linux/linux/include/generated/ "$scriptdir/linux/include/generated"
rsync -avz --delete-during -e "ssh -J pm3@lab.os.itec.kit.edu" pm3@pm3:/unser_zeug/git/deploy_script/repo/power_management_linux/linux/tools/include/generated/ "$scriptdir/linux/tools/include/generated"
rsync -avz --delete-during -e "ssh -J pm3@lab.os.itec.kit.edu" pm3@pm3:/unser_zeug/git/deploy_script/repo/power_management_linux/linux/tools/objtool/ "$scriptdir/linux/tools/objtool"
rsync -avz --delete-during -e "ssh -J pm3@lab.os.itec.kit.edu" pm3@pm3:/unser_zeug/git/deploy_script/repo/power_management_linux/linux/scripts/ "$scriptdir/linux/scripts"
rsync -avz --delete-during -e "ssh -J pm3@lab.os.itec.kit.edu" pm3@pm3:/unser_zeug/git/deploy_script/repo/power_management_linux/linux/Module.symvers "$scriptdir/linux/Module.symvers"
rm -R $scriptdir/linux/tools/objtool/libsubcmd
rm -f $scriptdir/linux/tools/objtool/*.o
rm -f $scriptdir/linux/tools/objtool/.*.o.cmd
rm -f $scriptdir/linux/scripts/*.o
rm -f $scriptdir/linux/scripts/.*.cmd
rm -f $scriptdir/linux/scripts/*/*.o
rm -f $scriptdir/linux/scripts/*/.*.cmd

echo "Done, you can now reboot the machine."
end_time=$(date +%s)
runtime=$((end_time - start_time))
minutes=$((runtime / 60))
seconds=$((runtime % 60))
echo "Script runtime: ${minutes} minutes and ${seconds} seconds"
