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
#include <stdint.h>
#include "mtd-abi.h"
#define CONFIG_YAFFS_DEFINES_TYPES 1
#define CONFIG_YAFFS_UTIL 1
#define YCHAR char
#define YUCHAR unsigned char 

//nand oob layout
static nand_ecclayout_t rb4xx_nand_oob_16 = {
	.eccbytes	= 6,
	.eccpos		= {8, 9, 10, 13, 14, 15},
	.oobfree	= { { 0, 4 }, { 6, 2 }, { 11, 2 }, { 4, 1 } },
};

#define noob rb4xx_nand_oob_16

size_t extract_tags_from_oob(unsigned char *dst, unsigned char *src,
                          size_t dst_len, nand_ecclayout_t *ecclayout){
  struct nand_oobfree *oobfree = ecclayout->oobfree;
  size_t rest_len = dst_len; //сколько места осталось
  size_t total_copied = 0; //сколько данных было скопировано
  size_t len; int a;
  void *s, *d = dst;
  for(a = 0; a < MTD_MAX_OOBFREE_ENTRIES; a++){
    /* копируем по кусочкам. кусочки(смещение и длина)
       описаны в массиве oobfree. */
    len = oobfree[a].length; //его длина
    if(len <= 0 || len > rest_len) break; //чуть что сразу выходим
    void *s = src + oobfree[a].offset; //начало данных кусочка
    memcpy(d, s, len); //копируем
    //printf("copy from offset = %lu to offset = %lu, bytes = %lu\n", s - (void*)src, d - (void*)dst, len);
    rest_len -= len;
    d += len;
    total_copied += len;
    //printf("rest_len = %lu\n", rest_len);
  }
  return total_copied;
}

/*
  несмотря на имя test.c это довольно сложна программа. это парсер структуры yaffs2 файловой системы создаваемой микротиком! изучи его код!
  так же можешь изучить код паковщика mkyaffs2image!
  obj_id == 16 это YAFFS_OBJECTID_SUMMARY 0x10. его создает новый yaffs. старый тот что использует микротик такого не делает.

*/

#include "yaffs_list.h"
#include "yaffs_guts.h"
#include "yaffs_packedtags1.h"
#include "yaffs_packedtags2.h"
#include "yaffs_tagscompat.h"

#include "k2m_biops.h"
#include "test_ecc_data.h"
#include "dumpers.h"

#define NAND_OOB_SIZE 16
#define NAND_PAGE_SIZE(x) (x + NAND_OOB_SIZE)

#define sectorsize (16384 + 32 * 16)
#define total_bytes_per_chunk 512
#define chunks_per_block (sectorsize / (total_bytes_per_chunk + NAND_OOB_SIZE))

//линейное расположение. без дыр!
unsigned int chunkToAddr(int chunkInNAND){
  return chunkInNAND * (total_bytes_per_chunk + NAND_OOB_SIZE);
}

void packedtags1_endian_convert(struct yaffs_packed_tags1 *pt){
  struct yaffs_packed_tags1 res;
  memset(&res, 0xFF, sizeof(res));
  res.chunk_id = get_be_bfv((*pt), chunk_id);
  res.serial_number = get_be_bfv((*pt), serial_number);
  res.n_bytes = get_be_bfv((*pt), n_bytes);
  res.obj_id = get_be_bfv((*pt), obj_id);
  res.ecc = get_be_bfv((*pt), ecc);
  res.deleted = get_be_bfv((*pt), deleted);
  memcpy((void *)pt, &res, sizeof(res)); 
}

int main(void){
  int fd;
  char *data;
  char *chunk;
  unsigned int total_size = 0;
  unsigned int total_chunks = 0;
  enum yaffs_ecc_result tags_ecc_result = 0;
  enum yaffs_ecc_result data_ecc_result = 0;
  struct yaffs_packed_tags1 *pt = NULL;
  struct yaffs_packed_tags1 rawpt;
  struct yaffs_packed_tags1 pt1; //size = 12 bytes !
  struct yaffs_ext_tags t; //extra flags. распаковывается ф-ей yaffs_unpack_tags2_tags_only из pt !
  unsigned int offset = 0;
  unsigned int addr;
  int a;
  int size;
  int chunkInNAND;
  char *x;
  printf("pt size = %lu\n", ((void*)&pt->should_be_ff) - ((void*)pt));
  //!!! конвертация в big endian !!! ЭТО ОЧЕНЬВАЖНЫЙ ФЛАГ !!! без него будет полный бред для big endian образов!
  to_big_endian = 1;
  data = malloc(100 * 1024 * 1024);
  if(!data){
    printf("Can't malloc memory!\n");
    exit(-1);
  }
  //fd = open("/home/prog/openwrt/work/rb941-2nd-mtd-dump/mtdblock2.bin", O_RDONLY);
//  fd = open("./kernel_nand.bin", O_RDONLY);
  //fd = open("./qqq-nand1.bin", O_RDONLY);
//  fd = open("/tmp/rrr/x2.bin", O_RDONLY);
//  fd = open("/tmp/rrr/kernel", O_RDONLY);
//  fd = open("/tmp/rrr/kernel-ok", O_RDONLY);
//  fd = open("/tmp/rrr/old-work", O_RDONLY);
  fd = open("/home/prog/openwrt/kernel2minor/xm.nand-tik-yaffs1-512b-ecc.bin", O_RDONLY);
//  fd = open("/home/prog/openwrt/pred-k2m/xm.nand-tik-yaffs1-512b-ecc.bin", O_RDONLY);
  if(fd < 0){
    printf("Can't open file!\n");
    exit(-1);
  }
  chunk = data;
  while((size = read(fd, chunk, total_bytes_per_chunk)) > 0){
    total_size += size;
    chunk += size;
  }
  close(fd);
  fd = creat("./unpacke-data.bin", O_WRONLY);
  //рассчет будет меньше если чанки не полностью заполняют все блоки! то есть если последний блок обрезан!
  printf("%u\n", total_size);
  total_chunks = total_size / sectorsize * chunks_per_block;
  printf("total_chunks = %u\n", total_chunks);
  if(total_chunks == 0 & total_size > 0) total_chunks = 1;
  //total_chunks = total_size / NAND_PAGE_SIZE(total_bytes_per_chunk);
  printf("I read %d bytes from data file! total_chunks = %d\n", total_size, total_chunks);
  //данные загрузили. работаем.
//  total_chunks = 64 + 20;
  for(chunkInNAND = 0; chunkInNAND < total_chunks; chunkInNAND++){
    addr = chunkToAddr(chunkInNAND);
    chunk = data + addr;
    //выдираем их отсюда. смотри yaffs2utils как пример. он тоже так делает. в pt же всего 8 байт
    //а функция распаковки рассчитывает на 12 !
    pt = (void*)(chunk + total_bytes_per_chunk + noob.oobfree[0].offset);
    memset(&pt1, 0xff, sizeof(struct yaffs_packed_tags1));
    extract_tags_from_oob((void*)&pt1, (void*)pt, 8, &noob);
    pt = &pt1;
    //dumper_q((void*)pt, 8);
    memcpy(&rawpt, pt, sizeof(rawpt)); //сохраним не сконвертированные в наш endian данные
    //dumper((void*)(realpt), 16);
    packedtags1_endian_convert(pt);
    //dumper((void*)(chunk + total_bytes_per_chunk), 16);
    //dumper((void*)(pt), 12);
    memset(&t, 0x0, sizeof(t));
    yaffs_unpack_tags1(&t, pt);
    //считаем ecc для данных
    data_ecc_result = check_ecc_data(chunk, total_bytes_per_chunk);
    write(fd, chunk, t.n_bytes); //write unpacked-data.bin
    //printf("chunk_id = %d\n", pt->chunk_id);
    printf("%u, %u: obj_id = %u, chunk_id = %u, n_bytes = %u, serial_num = %d, ecc = 0x%x(%s), ecc_data = %s, is_deleted = %u, chunk_used = %u\n",
           chunkInNAND, addr, t.obj_id, t.chunk_id, t.n_bytes, t.serial_number,
           pt->ecc, yaffs_check_tags_ecc((struct yaffs_tags*)&rawpt) == 0 ? "ok" : "ERROR",
           ecc_result2str(data_ecc_result),
           t.is_deleted, t.chunk_used);
    /*if(t.chunk_id == 0 && t.n_bytes == 0) //dump obj header(512 bytes)
      dumper4b((void*)chunk, 512); */
    //goto end; //!!!
  } 
end:
  close(fd);
  free(data);
  return 0;
}
