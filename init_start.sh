#!/bin/bash


echo "Load NVMeVirt kernel module.."
sudo insmod ./nvmev.ko	\
		 memmap_start=16G	\
		 memmap_size=32G	\
		 cpus=7,8
