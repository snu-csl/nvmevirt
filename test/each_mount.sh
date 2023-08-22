#!/bin/bash

if [ ! -d /disk1 ] ; then
	mkdir /disk1
fi

if [ ! -d /disk2 ] ; then
	mkdir /disk2
fi


sudo mkfs -t ext4 /dev/nvme0n1
file -s /dev/nvme0n1

sudo mkfs -t ext4 /dev/nvme1n2
file -s /dev/nvme1n2

mount /dev/nvme0n1 /disk1
mount /dev/nvme1n2 /disk2

df -h
