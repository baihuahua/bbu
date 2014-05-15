obj-m := bq34z100.o 
all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=`pwd` modules
clean:
	rm -f *.o *.ko *.mod.* Module* 
