#!/bin/bash

CREATE_VOX_FILE=~/.vox_PID*
NVMEV_PATH=~/Linux-study/linux/nvmev-mi
SHARE_FOLDER_PATH=~/Linux-study/nfs/
VOX_PAHT=~/Linux-study

if sudo ls ${CREATE_VOX_FILE}; then
	cd ${VOX_PATH}
	./vox k
fi

sudo cp -r ${NVMEV_PATH} ${SHARE_FOLDER_PATH}
