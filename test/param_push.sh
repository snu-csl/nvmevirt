#!/bin/bash

echo create memmap_start=35G memmap_size=4G cpus=1,1 > /sys/nvmevirt/config

sleep 1

echo create memmap_start=30G memmap_size=4G cpus=2,2 ftl=conv> /sys/nvmevirt/config

sleep 1

echo create memmap_start=25G memmap_size=4G cpus=3,3 > /sys/nvmevirt/config

sleep 1

echo create memmap_start=20G memmap_size=4G cpus=4,4 ftl=conv > /sys/nvmevirt/config
