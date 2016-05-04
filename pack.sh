#!/bin/sh

KERNEL_NOR="/home/prog/openwrt/trunk-rb941-2nd/bin/ar71xx/openwrt-ar71xx-mikrotik-vmlinux-lzma.elf"
KERNEL_NAND="/home/prog/openwrt/trunk/bin/ar71xx/openwrt-ar71xx-mikrotik-vmlinux-lzma.elf"

RESNAME_NOR=$(basename $KERNEL_NOR | sed -e 's/\..\+//')
RESNAME_NAND=$(basename $KERNEL_NAND | sed -e 's/\..\+//')

#for NOR
./kernel2minor -k $KERNEL_NOR -r ./$RESNAME_NOR-yaffs2-nor.bin -s 1024 -e
#for NAND
./kernel2minor -k $KERNEL_NAND -r ./$RESNAME_NAND-yaffs2-nand.bin -s 2048 -c -e
