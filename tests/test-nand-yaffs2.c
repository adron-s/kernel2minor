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

//nand oob layout
static nand_ecclayout_t nand_oob_64 = {
	.eccbytes	= 24,
	.eccpos		= {40, 41, 42, 43, 44, 45, 46, 47,
			   48, 49, 50, 51, 52, 53, 54, 55,
			   56, 57, 58, 59, 60, 61, 62, 63},
	.oobfree	= {{.offset = 2, .length = 38}},
};
#define noob nand_oob_64

/*
  несмотря на имя test.c это довольно сложна программа. это парсер структуры yaffs2 файловой системы создаваемой микротиком! изучи его код!
  так же можешь изучить код паковщика mkyaffs2image!
  obj_id == 16 это YAFFS_OBJECTID_SUMMARY 0x10. его создает новый yaffs. старый тот что использует микротик такого не делает.

*/

#include "yaffs_list.h"
#include "yaffs_guts.h"
#include "yaffs_packedtags2.h"

#include "k2m_biops.h"
#include "test_ecc_data.h"
#include "dumpers.h"

void pt_endianes_fix(struct yaffs_packed_tags2 *pt, int wich_one){
  if(wich_one == 0 || wich_one == 1){
    pt->t.seq_number = swap(pt->t.seq_number);
    pt->t.obj_id = swap(pt->t.obj_id);
    pt->t.chunk_id = swap(pt->t.chunk_id);
    pt->t.n_bytes = swap(pt->t.n_bytes);
  }
  //and ecc
  if(wich_one == 0 || wich_one == 2){
    pt->ecc.col_parity = swap(pt->ecc.col_parity);
    pt->ecc.line_parity = swap(pt->ecc.line_parity);
    pt->ecc.line_parity_prime = swap(pt->ecc.line_parity_prime);
  }
}

#define NAND_OOB_SIZE 64
#define NAND_PAGE_SIZE(x) (x + NAND_OOB_SIZE)

#define sectorsize 131072
#define total_bytes_per_chunk 2048
#define chunks_per_block (sectorsize / (total_bytes_per_chunk + NAND_OOB_SIZE))

unsigned int chunkToAddr(int chunkInNAND){
  return chunkInNAND * (total_bytes_per_chunk + NAND_OOB_SIZE);
}


char * yaffs_obj_type[] = {
  "UNKNOWN",
  "FILE",
  "SYMLINK",
  "DIRECTORY",
  "HARDLINK",
  "SPECIAL"
};

void print_extra(struct yaffs_ext_tags *t){
  char obj_type[32] = "ERROR_OBJ_TYPE";
  if(t->extra_obj_type >=0 || t->extra_obj_type < sizeof(yaffs_obj_type) / sizeof(*yaffs_obj_type)){
    strcpy(obj_type, yaffs_obj_type[t->extra_obj_type]);
  }
  printf("\textra_parent_id = %u, extra_is_shrink = %u, extra_shadows = %u, extra_obj_type = %s, extra_equiv_id = %u, extra_file_size = %lu\n",
    t->extra_parent_id, t->extra_is_shrink, t->extra_shadows, obj_type, t->extra_equiv_id, t->extra_file_size);

}

int main(void){
  int fd;
  char *data;
  char *chunk;
  unsigned int total_size = 0;
  unsigned int total_chunks = 0;
  enum yaffs_ecc_result tags_ecc_result = 0;
  enum yaffs_ecc_result data_ecc_result = 0;
  struct yaffs_packed_tags2 *pt = NULL;
  struct yaffs_ext_tags t; //extra flags. распаковывается ф-ей yaffs_unpack_tags2_tags_only из pt !
  unsigned int offset = 0;
  unsigned int addr;
  int a;
  int size;
  int chunkInNAND;
  char *x;
  printf("pt size = %lu\n", sizeof(*pt));
  //!!! конвертация в big endian !!! ЭТО ОЧЕНЬВАЖНЫЙ ФЛАГ !!! без него будет полный бред для big endian образов!
  to_big_endian = 1;
  data = malloc(32 * 1024 * 1024);
  if(!data){
    printf("Can't malloc memory!\n");
    exit(-1);
  }
  //fd = open("/home/prog/openwrt/work/rb941-2nd-mtd-dump/mtdblock2.bin", O_RDONLY);
//  fd = open("./kernel_nand.bin", O_RDONLY);
  fd = open("./qqq-nand2.bin", O_RDONLY);
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
  //рассчет будет меньше если чанки не полностью заполняют все блоки! то есть если последний блок обрезан!
  total_chunks = total_size / sectorsize * chunks_per_block;
  if(total_chunks == 0 & total_size > 0) total_chunks = 1;
  //total_chunks = total_size / NAND_PAGE_SIZE(total_bytes_per_chunk);
  printf("I read %d bytes from data file! total_chunks = %d\n", total_size, total_chunks);
  //данные загрузили. работаем.
//  total_chunks = 7874;
//  total_chunks = 189;
  for(chunkInNAND = 0; chunkInNAND < total_chunks; chunkInNAND++){
//  for(chunkInNAND = 0; chunkInNAND < 2; chunkInNAND++){
    //chunkInNAND = 202;
    addr = chunkToAddr(chunkInNAND);
    chunk = data + addr;
    pt = (void*)(chunk + total_bytes_per_chunk + noob.oobfree[0].offset);
    x = (void*)(&pt->t);
    //x = chunk;
    //printf("%lu -->\n", x - data);
    //преобразуем в наш endian но только блок tagsecc! без блока tags!
    pt_endianes_fix(pt, 2);
    memset(&t, 0x0, sizeof(t));
    yaffs_unpack_tags2(&t, pt, 1); //узнаем ecc result до преобразования tags в наш endians! так как он считается побайтово!
    tags_ecc_result = t.ecc_result;
    //теперь преобразуем tags в наш endian
    pt_endianes_fix(pt, 1);
    //распакуем. распаковка нужна так как могут быть дополнительные(extra) tags! запакованные в месте с нашими 4-мя полями(seq_number, ...)!
    memset(&t, 0x0, sizeof(t));
    yaffs_unpack_tags2(&t, pt, 1);
    //считаем ecc для данных. для tags его уже посчитала ф-я yaffs_unpack_tags2.
    data_ecc_result = check_ecc_data(chunk, total_bytes_per_chunk);
#ifdef ECC_ALGO_DEBUG
    dump_ecc_data(pt);
#endif
    printf("%u, %u: seq_num = %u, obj_id = %u, chunk_id = %u, n_bytes = %u, extra_avail = %u, tecc_res = %s, decc_res = %s\n", 
           chunkInNAND, addr, t.seq_number, t.obj_id,
           t.chunk_id, t.n_bytes, t.extra_available, ecc_result2str(tags_ecc_result), ecc_result2str(data_ecc_result));
    if(t.extra_available){
      print_extra(&t);
    }
    //printf("0x%x: ", addr); for(a = 0; a < 16; a++){ printf("%3x", x[a] & 0xFF); } printf("\n");
  }
end:
  free(data);
  return 0;
}
