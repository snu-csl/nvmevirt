#!/bin/bash

DIRNAME=/home/toor/nvmevirt

MEMMAP_START=7
MEMMAP_SIZE=1

cd $DIRNAME
make \
    && sudo cp ${DIRNAME}/nvmev.ko /lib/modules/$(uname -r)/misc/ \
    && sudo depmod -a \
    && sudo modprobe -r nvmev \
    && sleep 1 \
    && sudo modprobe nvmev memmap_start=${MEMMAP_START}G memmap_size=${MEMMAP_SIZE}G cpus=2,3
