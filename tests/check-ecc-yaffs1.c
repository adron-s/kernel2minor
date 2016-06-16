#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include "mtd-abi.h"
#define CONFIG_YAFFS_DEFINES_TYPES 1
#define CONFIG_YAFFS_UTIL 1
#define YCHAR char
#define YUCHAR unsigned char

#include "k2m_biops.h"
#include "yaffs_list.h"
#include "yaffs_guts.h"
#include "yaffs_packedtags1.h"
#include "yaffs_packedtags2.h"
#include "yaffs_tagscompat.h"

void dumper(char *buf, int size){
  int a;
  printf("Dumping memory: 0x%p, size: %d\n", buf, size);
  for(a = 0; a < size; a++){
    if(a % 8 == 0) printf("\n");
    printf("%02x ", buf[a] & 0xFF);
  }
  printf("\n-----------------------------\n");
}

unsigned char tags1_data[ ] = { 0x00, 0x00, 0x04, 0x00, 0x00, 0x40, 0x40, 0xc6 };

/********** Tags ECC calculations  *********/

void yaffs_calc_tags_ecc(struct yaffs_tags *tags)
{
	/* Calculate an ecc */
	unsigned char *b = ((union yaffs_tags_union *)tags)->as_bytes;
	unsigned i, j;
	unsigned ecc = 0;
	unsigned bit = 0;

	set_be_bfv((*tags), ecc, 0);

	for (i = 0; i < 8; i++) {
		for (j = 1; j & 0xff; j <<= 1) {
			bit++;
			if (b[i] & j)
				ecc ^= bit;
		}
	}
        set_be_bfv((*tags), ecc, ecc);
}

int yaffs_check_tags_ecc(struct yaffs_tags *tags)
{
	unsigned ecc = get_be_bfv((*tags), ecc);

	yaffs_calc_tags_ecc(tags);

	ecc ^= get_be_bfv((*tags), ecc);

	if (ecc && ecc <= 64) {
		/* TODO: Handle the failure better. Retire? */
		unsigned char *b = ((union yaffs_tags_union *)tags)->as_bytes;

		ecc--;

		b[ecc / 8] ^= (1 << (ecc & 7));

		/* Now recvalc the ecc */
		yaffs_calc_tags_ecc(tags);

		return 1;	/* recovered error */
	} else if (ecc) {
		/* Wierd ecc failure value */
		/* TODO Need to do somethiong here */
		return -1;	/* unrecovered error */
	}
	return 0;
}

int main(void){
  //set_be_bfv((*(struct yaffs_tags*)tags1_data), ecc, 0xff);
  printf("endian_need_conv = %d\n", endian_need_conv);
  dumper((void*)tags1_data, sizeof(tags1_data));
  printf("ecc check result = %d\n", yaffs_check_tags_ecc((struct yaffs_tags*)tags1_data));
  printf("size = %lu\n", sizeof(struct yaffs_obj_hdr));
  return 0;
}
