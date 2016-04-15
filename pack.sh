#!/bin/sh

KERNEL="/home/prog/openwrt/trunk-rb941-2nd/bin/ar71xx/openwrt-ar71xx-mikrotik-vmlinux-lzma.elf"

RESNAME=$(basename $KERNEL | sed -e 's/\..\+//')
./kernel2minor -k $KERNEL -r ./$RESNAME-yaffs2.bin -e
