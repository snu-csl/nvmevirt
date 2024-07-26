#!/bin/bash

DIRNAME=/home/toor/nvmevirt

sudo mkdir -pv /lib/modules/$(uname -r)/misc
sudo cp ${DIRNAME}/nvmev.ko /lib/modules/$(uname -r)/misc/ \
    && sudo depmod -a
