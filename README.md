Hello.

MiNor means Mikrotik Nor :-) because initially this project was done for Mirotik NOR flash systems.
However, later I add support for NAND flash systems too.

This program packs OpenWrt Linux Kernel to Mikrotik's version of yaffs2 file system which used by the mikrotik's routerboot for booting.
So you do not need to include yaffs2 support in the linux kernel any more.

Currently supported flashes are:
  NOR flash:
    Mikrotik rb941-2nd(hAP lite)
    And maby(not tested yet) all new routerboards with this strings in description:
      Storage type     FLASH
      Storage size     16 MB
  NAND flash:
    Mikrotik rb750 and rb751
    And maby(not tested yet) all routerboards with NAND flash parameters like this:
      Eraseblock size:                131072 bytes, 128.0 KiB
      Minimum input/output unit size: 2048 bytes
      OOB size:                       64 bytes
