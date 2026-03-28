obj-m := ws_dsi_panel.o

# Point KDIR at your kernel source tree (must match the target's running kernel)
KDIR  ?= /root/orangepi-kernel
CROSS ?= aarch64-linux-gnu-
ARCH  ?= arm64

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS) clean
