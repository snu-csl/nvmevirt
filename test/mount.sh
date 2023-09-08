#!/bin/bash

case $1 in

-1)
	sudo mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 /dev/nvme0n1
	mount /dev/nvme0n1 /mnt/disk1
	exit 0
	;;
-2)
	sudo mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 /dev/nvme0n1
	sudo mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 /dev/nvme1n2
	mount /dev/nvme0n1 /mnt/disk1
	mount /dev/nvme1n2 /mnt/disk2
	exit 0
	;;
-3)
	sudo mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 /dev/nvme0n1
	sudo mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 /dev/nvme1n2
	sudo mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 /dev/nvme2n3
	mount /dev/nvme0n1 /mnt/disk1
	mount /dev/nvme1n2 /mnt/disk2
	mount /dev/nvme2n3 /mnt/disk3
esac


