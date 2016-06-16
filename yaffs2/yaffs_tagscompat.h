/*
 * это слегка измененный файл взятый из исходников yaffs2.
 * я вынужден был его изменить из за необходимости считать ecc
 * для переконвертированных в другой endian yaffs1_tags.
 */

#ifndef __YAFFS_TAGSCOMPAT_H__
#define __YAFFS_TAGSCOMPAT_H__

void yaffs_calc_tags_ecc(struct yaffs_tags *tags);
int yaffs_check_tags_ecc(struct yaffs_tags *tags);

#endif
