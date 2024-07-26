#!/bin/sh

MEMMAP_START=$1
MEMMAP_SIZE=$2

sudo sed -Ei \
    's|^GRUB_CMDLINE_LINUX="(.*)"$|GRUB_CMDLINE_LINUX="memmap='$MEMMAP_SIZE'G\\\\\\$'$MEMMAP_START'G"|g' \
    /etc/default/grub

sudo update-grub

cat << EOF | sudo tee /etc/modules-load.d/nvmev.conf
nvmev
EOF

cat << EOF | sudo tee /etc/modprobe.d/nvmev.conf
options nvmev memmap_start=${MEMMAP_START}G memmap_size=${MEMMAP_SIZE}G cpus=0,1
EOF
