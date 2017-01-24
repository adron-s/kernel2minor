#!/bin/sh
#align. It's important for openwrt's combined image.
CI_BLKSZ=65536

KERNEL="./xm.elf"
#KERNEL="/home/prog/openwrt/trunk-rb941-2nd/bin/ar71xx/openwrt-ar71xx-mikrotik-vmlinux-lzma.elf"

RESNAME=$(basename $KERNEL | sed -e 's/\..\+//')

#for NOR. info block align == 0 because: image_size % 65536 == 0
#./kernel2minor -k $KERNEL -r ./$RESNAME.nor-tik-yaffs2.bin -s 1024 -i 0 -p NOR01024
#./kernel2minor -k $KERNEL -r ./$RESNAME.nor-tik-yaffs2.bin -s 1024 -p NOR01024

#for NAND-2048(new rb7xx and higher)
#./kernel2minor -k $KERNEL -r ./$RESNAME.nand-tik-yaffs2-2048b-ecc.bin -s 2048 -i $CI_BLKSZ -p NAND0800 -c
#./kernel2minor -k $KERNEL -r ./$RESNAME.nand-tik-yaffs2-2048b-ecc.bin -s 2048 -p NND02048 -c

#for NAND-512(old rb4xx)
#./kernel2minor -k $KERNEL -r ./$RESNAME.nand-tik-yaffs1-512b-ecc.bin -s 512 -i $CI_BLKSZ -p NND00512 -c
./kernel2minor -k $KERNEL -r ./$RESNAME.nand-tik-yaffs1-512b-ecc.bin -s 512 -e -c
#./kernel2minor -k $KERNEL -r ./$RESNAME.nand-tik-yaffs1-512b-ecc.bin -s 512 -p NND00512 -c
