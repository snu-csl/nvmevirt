#!/bin/sh

MEMMAP_START=7
MEMMAP_SIZE=1

sudo modprobe nvmev memmap_start=${MEMMAP_START}G memmap_size=${MEMMAP_SIZE}G cpus=2,3
