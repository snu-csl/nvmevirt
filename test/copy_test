#!/bin/bash

echo "Second Testing"
echo "After mounting the two folders, check if the copy works well for each other"

if [ ! -d /mnt/disk1 ] ; then
	mkdir /mnt/disk1
fi

if [ ! -d /mnt/disk2 ] ; then
	mkdir /mnt/disk2
fi


sudo mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 /dev/nvme0n1
file -s /dev/nvme0n1


sudo mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 /dev/nvme1n2
file -s /dev/nvme1n2

umount /mnt/disk1
sleep 1
umount /mnt/disk2

mount /dev/nvme0n1 /mnt/disk1
mount /dev/nvme1n2 /mnt/disk2

df -h

echo ""
echo ""
echo ""

echo "copy test"
cd /mnt/disk1
sudo touch moving
sudo echo "success" > moving

sudo cp moving /mnt/disk2/moving

if [ "`sudo cat /mnt/disk2/moving`" == "success" ]; then
	echo "just copy, Success!"
else
	echo "just copy, Fail..."
fi

echo ""
echo "after umount test"

umount /mnt/disk1
sleep 1
umount /mnt/disk2

mount /dev/nvme0n1 /mnt/disk1
mount /dev/nvme1n2 /mnt/disk2

if [ "`sudo cat /mnt/disk1/moving`" == "success" ]; then
	echo "after umount disk1, Success!"
else
	echo "after umount disk1, Fail..."
fi

if [ "`sudo cat /mnt/disk2/moving`" == "success" ]; then
	echo "after umount disk2, Success!"
else
	echo "after umount disk2, Fail..."
fi
