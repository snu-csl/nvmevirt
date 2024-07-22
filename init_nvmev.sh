#!/bin/bash

#sudo rmmod nvmev

TARGET=$1

#echo "Load nvme module"
make clean
make ID=$TARGET || exit

echo "Load NVMeVirt kernel module...", $TARGET

if [ $TARGET -eq 0 ]
then
	sudo rmmod nvmev0

	sudo insmod nvmev0.ko \
		memmap_start=128G \
		memmap_size=4G \
		cpus=1,2
elif [ $TARGET -eq 1 ]
then
	sudo rmmod nvmev1

	sudo insmod nvmev1.ko \
		memmap_start=133G \
		memmap_size=4G \
		cpus=3,4
elif [ $TARGET -eq 2 ]
then
	sudo rmmod nvmev2

	sudo insmod nvmev2.ko \
		memmap_start=138G \
		memmap_size=4G \
		cpus=5,6
elif [ $TARGET -eq 3 ]
then
	sudo rmmod nvmev3

	sudo insmod nvmev3.ko \
		memmap_start=143G \
		memmap_size=4G \
		cpus=5,6
fi


#./set_perf.py 12 12 2460
