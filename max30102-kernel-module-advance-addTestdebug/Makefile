obj-m += max30102_driver.o
max30102_driver-objs := max30102_core.o max30102_i2c.o max30102_interrupt.o max30102_config.o max30102_data.o max30102_ioctl.o max30102_debug.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f *.ko *.mod.c *.o Module.symvers modules.order

install: all
	sudo insmod max30102_driver.ko