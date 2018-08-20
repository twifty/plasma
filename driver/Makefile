ifeq ($(KERNELRELEASE),)

	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules EXTRA_CFLAGS="-g -DDEBUG"

clean:
	rm -rf *.ko *.o *~ core .depend .*.cmd *.mod.c .tmp_versions *.symvers *.order

install: clean all
	rmmod plasma.ko || true
	insmod plasma.ko
	rm -rf *.o *~ core .depend .*.cmd *.mod.c .tmp_versions *.symvers *.order

.PHONY: all clean

else
	# called from kernel build system: just declare what our modules are
	obj-m := plasma.o
endif
