# Makefile for Custom NVMe PCIe Driver

# Kernel build directory - adjust as needed
KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build

# Module name
obj-m := bar_rw_driver.o nvme_custom_driver.o

# Build flags
ccflags-y := -DDEBUG

# Build target
all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f test_nvme_custom nvme_test_app

install: all
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
	depmod -a

uninstall:
	rm -f /lib/modules/$(shell uname -r)/extra/nvme_simple_driver.ko
	depmod -a

# Load/unload module
load:
	insmod nvme_simple_driver.ko

unload:
	rmmod nvme_simple_driver

# Show module info
info:
	modinfo nvme_simple_driver.ko

# Check if device is detected
check:
	lspci | grep -i "15:00.0"
	dmesg | tail -20 | grep nvme_simple
	lsblk | grep nvme_simple

# Build test application
test_app:
	gcc -o nvme_test_app nvme_test_app.c -O2

.PHONY: all clean install uninstall load unload info check test_app