/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2011 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Это измененный файл взятый из исходников yaffs2.
 * я вынужден был его изменить из за необходимости считать ecc
 * для переконвертированных в другой endian tags.
 *
 */

#include "yaffs_guts.h"
#include "yaffs_tagscompat.h"
#include "yaffs_ecc.h"
#include "../k2m_biops.h"


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
