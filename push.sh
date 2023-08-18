#!/bin/bash

echo create memmap_start=32G memmap_size=4G cpus=1,1 > /sys/nvmevirt/config

sleep 1

echo create memmap_start=28G memmap_size=2G cpus=1,1 > /sys/nvmevirt/config
