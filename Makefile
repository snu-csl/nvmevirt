KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD     := $(shell pwd)

# Select one of the targets to build
CONFIG_NVMEVIRT_NVM := y
#CONFIG_NVMEVIRT_SSD := y
#CONFIG_NVMEVIRT_ZNS := y
#CONFIG_NVMEVIRT_KVSSD := y  # KVSSD will be available shortly after cleaning up the code


obj-m   := nvmev.o
nvmev-objs := main.o pci.o admin.o io.o dma.o

ccflags-$(CONFIG_NVMEVIRT_NVM) += -DBASE_SSD=INTEL_OPTANE
nvmev-$(CONFIG_NVMEVIRT_NVM) += simple_ftl.o

ccflags-$(CONFIG_NVMEVIRT_SSD) += -DBASE_SSD=SAMSUNG_970PRO
nvmev-$(CONFIG_NVMEVIRT_SSD) += ssd.o conv_ftl.o pqueue.o channel_model.o

ccflags-$(CONFIG_NVMEVIRT_ZNS) += -DBASE_SSD=ZNS_PROTOTYPE
nvmev-$(CONFIG_NVMEVIRT_ZNS) += ssd.o zns_ftl.o zns_read_write.o zns_mgmt_send.o zns_mgmt_recv.o channel_model.o

ccflags-y += -Wno-implicit-fallthrough -Wno-unused-function -Wno-declaration-after-statement -Wno-unused-variable

default:
		$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

install:
	   $(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

.PHONY: clean
clean:
	   $(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	   rm -f cscope.out tags

.PHONY: cscope
cscope:
		cscope -b -R
		ctags *.[ch]
