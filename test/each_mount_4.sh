#!/bin/bash

echo "Fourth Testing"
echo "Check random read/write performance with together"

if [ ! -d /mnt/disk1 ] ; then
	mkdir /mnt/disk1
fi

if [ ! -d /mnt/disk2 ] ; then
	mkdir /mnt/disk2
fi

if [ ! -d /mnt/disk3 ] ; then
	mkdir /mnt/disk3
fi

sudo mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 /dev/nvme0n1
file -s /dev/nvme0n1


sudo mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 /dev/nvme1n2
file -s /dev/nvme1n2

sudo mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 /dev/nvme2n3
file -s /dev/nvme2n3

mount /dev/nvme0n1 /mnt/disk1
mount /dev/nvme1n2 /mnt/disk2
mount /dev/nvme2n3 /mnt/disk3

df -h

echo "simple random read and conv random read"
echo ""
sudo fio --directory=/mnt/disk1 --name fio_simple_read --direct=1 --rw=randread --bs=4K --size=3G --time_based --runtime=180 --group_reporting --norandommap &

sudo fio --directory=/mnt/disk3 --name fio_conv_read --direct=1 --rw=randread --bs=4K --size=3G --time_based --runtime=180 --group_reporting --norandommap &







