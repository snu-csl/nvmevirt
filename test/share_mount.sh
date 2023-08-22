#!/bin/bash

if [ ! -d /disk3 ] ; then
	mkdir /disk3
fi

sudo mkfs -t ext4 /dev/nvme0n1
file -s /dev/nvme0n1

sudo mkfs -t ext4 /dev/nvme1n2
file -s /dev/nvme1n2

mount /dev/nvme0n1 /disk3
mount /dev/nvme1n2 /disk3

df -h
