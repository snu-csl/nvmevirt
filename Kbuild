NAME = nvmev
ID = 0

TARGET = $(join ${NAME}, ${ID})

# Select one of the targets to build
#CONFIG_NVMEVIRT_NVM := y
CONFIG_NVMEVIRT_SSD := y
#CONFIG_NVMEVIRT_ZNS := y
#CONFIG_NVMEVIRT_KV := y

obj-m   := $(TARGET).o
$(TARGET)-objs := main.o pci.o admin.o io.o dma.o
ccflags-y += -Wno-unused-variable -Wno-unused-function

ccflags-$(CONFIG_NVMEVIRT_NVM) += -DBASE_SSD=INTEL_OPTANE -DVIRT_ID=${ID}
$(TARGET)-$(CONFIG_NVMEVIRT_NVM) += simple_ftl.o

ccflags-$(CONFIG_NVMEVIRT_SSD) += -DBASE_SSD=SAMSUNG_970PRO -DVIRT_ID=${ID}
$(TARGET)-$(CONFIG_NVMEVIRT_SSD) += ssd.o conv_ftl.o pqueue/pqueue.o channel_model.o

ccflags-$(CONFIG_NVMEVIRT_ZNS) += -DBASE_SSD=WD_ZN540 -DVIRT_ID=${ID}
#ccflags-$(CONFIG_NVMEVIRT_ZNS) += -DBASE_SSD=ZNS_PROTOTYPE
ccflags-$(CONFIG_NVMEVIRT_ZNS) += -Wno-implicit-fallthrough
$(TARGET)-$(CONFIG_NVMEVIRT_ZNS) += ssd.o zns_ftl.o zns_read_write.o zns_mgmt_send.o zns_mgmt_recv.o channel_model.o

ccflags-$(CONFIG_NVMEVIRT_KV) += -DBASE_SSD=KV_PROTOTYPE -DVIRT_ID=${ID}
$(TARGET)-$(CONFIG_NVMEVIRT_KV) += kv_ftl.o append_only.o bitmap.o
