#!/bin/sh

KERNEL="/home/prog/openwrt/openwrt-all/bin/ar71xx/openwrt-ar71xx-mikrotik-vmlinux-lzma.elf"
#KERNEL="/home/prog/openwrt/trunk-rb941-2nd/bin/ar71xx/openwrt-ar71xx-mikrotik-vmlinux-lzma.elf"

RESNAME=$(basename $KERNEL | sed -e 's/\..\+//')

#for NOR
#./kernel2minor -k $KERNEL -r ./$RESNAME.nor-tik-yaffs2.bin -s 1024 -e -v
#for NAND
./kernel2minor -k $KERNEL -r ./$RESNAME.nand-tik-yaffs2-2048b-ecc.bin -s 2048 -i 65536 -c -e
