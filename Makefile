# Main makefile
#
# Copyright (C) 2007 Beihang University
# Written by Zhu Like ( zlike@cse.buaa.edu.cn )
#

drivers_dir       := drivers
boot_dir          := boot
init_dir          := init
lib_dir           := lib
mm_dir            := mm
tools_dir         := tools
vmlinux_elf       := gxemul/vmlinux

link_script   := $(tools_dir)/scse0_3.lds

modules           := boot drivers init lib mm
objects           := $(boot_dir)/start.o                   \
                                 $(init_dir)/*.o           \
                                 $(drivers_dir)/gxconsole/console.o \
                                 $(lib_dir)/*.o            \
                                 $(mm_dir)/*.o

.PHONY: all $(modules) clean

all: $(modules) vmlinux

vmlinux: $(modules)
		$(LD) -o $(vmlinux_elf) -N -T $(link_script) $(objects)

$(modules):
		$(MAKE) --directory=$@

clean:
		for d in $(modules);    \
                do                                      \
                        $(MAKE) --directory=$$d clean; \
                done; \
        rm -rf *.o *~ $(vmlinux_elf)

include include.mk

