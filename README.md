# NVMeVirt

### Introduction


### Configuring and Compiling

- Recommend kernel v5.15.x (mainly tested on 5.15.37)

- A part of main memory should be reserved for the storage of emulated device. To this end, add following options to `GRUB_CMDLINE_LINUX` in `/etc/default/grub` as follow:

  ```bash
  GRUB_CMDLINE_LINUX="memmap=64G\\\$128G"
  ```
- This option will reserve 64GB of memory chunk starting from 128GB memory offset. You may change the values according to your situation. And update the bootloader configuration.
  ```bash
  $ sudo update-grub
  ```


- Currently, you need to select the target device type by manually editing the Makefile. You may find the following lines in the Makefile, which implies that NVMeVirt is currently configured for emulating NVM SSD. You may uncomment other one to change the target type. Note that you should uncomment one device type at a time.
  ```Makefile
  # Select one of the targets to build
  CONFIG_NVMEVIRT_NVM := y
  #CONFIG_NVMEVIRT_SSD := y
  #CONFIG_NVMEVIRT_ZNS := y
  #CONFIG_NVMEVIRT_KVSSD := y
  ```
- You may find the detailed configuration parameters from `ssd_config.h`.

- Compile. Make sure the kernel source in the Makefile is what you want to use

  ```bash
  $ make
  make -C /lib/modules/5.15.37/build M=/path/to/nvmev modules
  make[1]: Entering directory '/path/to/linux-5.15.37'
    CC [M]  /path/to/nvmev/main.o
    CC [M]  /path/to/nvmev/pci.o
    CC [M]  /path/to/nvmev/admin.o
    CC [M]  /path/to/nvmev/io.o
    CC [M]  /path/to/nvmev/dma.o
    CC [M]  /path/to/nvmev/simple_ftl.o
    LD [M]  /path/to/nvmev/nvmev.o
    MODPOST /path/to/nvmev/Module.symvers
    CC [M]  /path/to/nvmev/nvmev.mod.o
    LD [M]  /path/to/nvmev/nvmev.ko
    BTF [M] /path/to/nvmev/nvmev.ko
  make[1]: Leaving directory '/path/to/linux-5.15.37'
  $
  ```

### Running

- Load the module.
  ```bash
  $ sudo insmod ./nvmev.ko \
      memmap_start=128  \ # in GiB
      memmap_size=65536 \ # in MiB
      cpus=1,3,17,19    \ # List of CPUs to process I/O requests (should have at least 2)
      read_latency=1000 \ # in usec
      write_latency=1000 \ # in usec
      io_unit_size=4096 \ # I/O unit
      nr_io_units=8			  # Number of I/O units that can be accessed in parallel
  ```
`memmap_start` and `memmap_size` should be matched to the ones set by the bootloader. You will see a new device is created as `/dev/nvmen[0-9]`.
