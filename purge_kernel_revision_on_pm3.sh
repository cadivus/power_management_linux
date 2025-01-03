#!/usr/bin/env bash

set -e

for revision in "$@"; do
	printf "Purging %s\n" "$revision"

	purge_cmds="$(cat <<-END
	rm -f "/boot/initramfs-$revision.img"
	rm -f "/boot/vmlinuz-$revision"
	rm -rf "/usr/lib/modules-$revision/"
	rm -rf "/usr/src/kernels/$revision/"
	END
	)"

	#printf "%s\n" "$purge_cmds"
	ssh -J pm3@lab.os.itec.kit.edu pm3@pm3 "bash -c \"sudo -S <<< \"pm3\" sh -c '$purge_cmds'\""
done
