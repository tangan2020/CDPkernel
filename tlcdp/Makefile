# Makefile 2.6
# http://blog.chinaunix.net/uid-11572501-id-2868714.html  ¹ØÓÚ stabs
#debug switch

# get kernel version
KVER := $(shell uname -r | cut -d'-' -f1)
KVER_MAJOR := $(shell echo $(KVER) | cut -d'.' -f1)
KVER_MINOR := $(shell echo $(KVER) | cut -d'.' -f2)

ifeq ($(DEBUG), y)
	ccflags-y += -O0 -g -DTRANS_DEBUG
	ifeq ($(shell [ $(KVER_MAJOR) -lt 4 ] && echo yes),yes)
    ccflags-y += -gstabs
  endif
endif

obj-m += cdp_filter.o
KDIR:=/lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(PWD) modules
clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -rf *.symvers *.markers
	
