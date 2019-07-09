KERNEL_VER = `uname -r`
BUILD = `date +%Y%m%d.%k%m`

ccflags-y := -g -Wall

obj-m += dummymapmod.o

dummymapmod-objs := dummymap.o
all: dummymap.ko

dummymap.ko:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean

client:
	gcc -O2 -o test-vfio ./test-vfio.c

.PHONY: client
