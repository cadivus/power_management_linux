#!/usr/bin/env bash

set -e

for revision in "$@"; do
	printf "Purging %s\n" "$revision"

	purge_cmds="$(cat <<-END
	rm -f "/boot/initramfs-$revision.img"
	rm -f "/boot/vmlinuz-$revision"
	rm -f "/boot/loader/entries/*-$revision.conf"
	rm -rf "/usr/lib/modules/$revision/"
	rm -rf "/usr/src/kernels/$revision/"
	END
	)"

	#printf "%s\n" "$purge_cmds"
	ssh -J pm3@lab.os.itec.kit.edu pm3@pm3 "bash -c \"sudo -S <<< \"pm3\" sh -c '$purge_cmds'\""
done

# Supposedly it is not neccessary to update grub, as it auto-detects installed kernels?
#printf "Updating GRUB2 menu entries\n"
#ssh -J pm3@lab.os.itec.kit.edu pm3@pm3 "bash -c \"cd / && sudo -S <<< \"pm3\" grub2-mkconfig -o /boot/grub2/grub.cfg\""

