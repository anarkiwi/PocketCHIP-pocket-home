export CONFIG:=Release

PKG_CONFIG:=$(shell which pkg-config)

PKG_CONFIG_PACKAGES = \
	alsa \
	gio-2.0 \

export PKG_CONFIG_CFLAGS=$(foreach pkg, $(PKG_CONFIG_PACKAGES), $(shell $(PKG_CONFIG) --cflags $(pkg)))
# -li2c: the i2c_smbus_* helpers moved out of <linux/i2c-dev.h> into libi2c
# (modern i2c-tools); it has no pkg-config file, so append it directly.
export PKG_CONFIG_LDFLAGS=$(foreach pkg, $(PKG_CONFIG_PACKAGES), $(shell $(PKG_CONFIG) --libs $(pkg))) -li2c



# DEPFLAGS= disables the Projucer Makefile's -MMD dependency generation. We
# always build from a clean tree (fresh container / dpkg clean), so the per-TU
# .d files buy nothing, and under parallel make (-jN) they race the per-rule
# `mkdir -p` of build/intermediate -> intermittent
# "fatal error: opening dependency file ...: No such file or directory".
# Command-line vars override the sub-Makefile's `:=`, so this kills the race.
#
# TARGET_ARCH overrides the Projucer Makefile's `-march=native` default (it only
# sets that `ifeq ($(TARGET_ARCH),)`, so a non-empty value here wins). `native`
# is poison: in the emulated arm/v7 build container gcc mis-detects the CPU and
# emits instructions (VFPv4 vfma etc.) the CHIP's Cortex-A8 lacks -> the binary
# links fine but dies with "Illegal instruction" (SIGILL) on first run. The R8 is
# always a Cortex-A8 (ARMv7-A + NEON, VFPv3), so target it exactly.
all:
	cd Builds/LinuxMakefile && $(MAKE) DEPFLAGS= TARGET_ARCH='-mcpu=cortex-a8 -mfpu=neon -mfloat-abi=hard'

clean:
	cd Builds/LinuxMakefile && $(MAKE) clean

wifitest:
	cd Builds/LinuxMakefile && $(MAKE) -f UnitTests.mk ../../build/$(CONFIG)/wifitest

devinstall:
	killall pocket-home ;\
	sudo cp build/$(CONFIG)/pocket-home /usr/bin/pocket-home && \
	sudo cp -R assets/* /usr/share/pocket-home
