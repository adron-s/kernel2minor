#!/bin/bash

#определим путь до директории с kernel2minor/tests
workdir=$(dirname $(ls -l ./${0} | sed -e 's/^.\+-> //'))

#to upcase
target="${1,,}"
[ "$target" == "nor" ] || [ "$target" == "nand-yaffs2" ] ||  [ "$target" == "nand-yaffs1" ] && {
  echo "Switching to target := $target"
  rm -f ./test.c
  ln -s $workdir/test-$target.c ./test.c
}

gcc ./test.c -o ./test ./yaffs_packedtags1.o ./yaffs_packedtags2.o \
    ./yaffs_tagscompat.o ./yaffs_ecc.o ./yaffs_hweight.o
