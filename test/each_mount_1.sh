#!/bin/bash

echo "First Testing"
echo "Check that this feature is working properly"

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

echo "nvme0 test"
cd /mnt/disk1
sudo touch test_nvme0n1
sudo echo "success" > test_nvme0n1

if [ "`sudo cat test_nvme0n1`" == "success" ]; then
	echo "nvme0 Success!"
else
	echo "nvme0 Fail..."
fi

echo ""

echo "nvme1 test"
cd /mnt/disk2
sudo touch test_nvme1n2
sudo echo "success" > test_nvme1n2

if [ "`sudo cat test_nvme1n2`" == "success" ]; then
	echo "nvme1 Success!"
else
	echo "nvme1 Fail..."
fi

