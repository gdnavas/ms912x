ms912x-y := \
	ms912x_registers.o \
	ms912x_connector.o \
	ms912x_transfer.o \
	ms912x_drv.o

obj-m := ms912x.o

KVER ?= $(shell uname -r)
KSRC ?= /lib/modules/$(KVER)/build

# Use clang if the kernel was built with clang
CC ?= clang

all:	modules

modules:
	make CC=$(CC) LD=ld.lld -C $(KSRC) M=$(PWD) modules

clean:
	make -C $(KSRC) M=$(PWD) clean
	rm -f $(PWD)/Module.symvers $(PWD)/*.ur-safe
