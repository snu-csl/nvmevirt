KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD     := $(shell pwd)

default:
		$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
		cp nvmev.ko ~/share/modules/nvmev.ko

install:
	   $(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

.PHONY: clean
clean:
	   $(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	   rm -f cscope.out tags nvmev.S

.PHONY: cscope
cscope:
		cscope -b -R
		ctags *.[ch]

.PHONY: tags
tags: cscope

.PHONY: format
format:
	clang-format -i *.[ch]

.PHONY: dis
dis:
	objdump -d -S nvmev.ko > nvmev.S
