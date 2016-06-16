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
#define CONFIG_YAFFS_DEFINES_TYPES 1
#define CONFIG_YAFFS_UTIL 1
#define YCHAR char
#define YUCHAR unsigned char 
#define SWAP32(x)   ((((x) & 0x000000FF) << 24) | \
                     (((x) & 0x0000FF00) << 8 ) | \
                     (((x) & 0x00FF0000) >> 8 ) | \
                     (((x) & 0xFF000000) >> 24))

#define SWAP16(x)   ((((x) & 0x00FF) << 8) | \
                     (((x) & 0xFF00) >> 8))


/*
  несмотря на имя test.c это довольно сложна программа. это парсер структуры yaffs2 файловой системы создаваемой микротиком! изучи его код!
  так же можешь изучить код паковщика mkyaffs2image!
  obj_id == 16 это YAFFS_OBJECTID_SUMMARY 0x10. его создает новый yaffs. старый тот что использует микротик такого не делает.

*/

#include "yaffs_list.h"
#include "yaffs_guts.h"
#include "yaffs_packedtags2.h"

void pt_endianes_fix(struct yaffs_packed_tags2_tags_only *pt){
  pt->seq_number = SWAP32(pt->seq_number);
  pt->obj_id = SWAP32(pt->obj_id);
  pt->chunk_id = SWAP32(pt->chunk_id);
  pt->n_bytes = SWAP32(pt->n_bytes);
}

#define NOR_OOB_SIZE 16
#define NOR_PAGE_SIZE(x) (x + NOR_OOB_SIZE)

#define sectorsize (64 * 1024)
#define total_bytes_per_chunk 1024
#define chunks_per_block (sectorsize / (total_bytes_per_chunk + 16))

unsigned int chunkToAddr(int chunkInNAND){
  //такая странная формула чтобы пропускать в каждом блоке по 16 байт в конце!
  return sectorsize * (chunkInNAND / chunks_per_block)
         + NOR_PAGE_SIZE(total_bytes_per_chunk)
         * (chunkInNAND % chunks_per_block);
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
  struct yaffs_packed_tags2_tags_only *pt = NULL; // 16 bytes
  struct yaffs_ext_tags t; //extra flags. распаковывается ф-ей yaffs_unpack_tags2_tags_only из pt !
  unsigned int offset = 0;
  unsigned int addr;
  int a;
  int size;
  int chunkInNAND;
  char *x;
  printf("pt size = %lu\n", sizeof(*pt));
  data = malloc(32 * 1024 * 1024);
  if(!data){
    printf("Can't malloc memory!\n");
    exit(-1);
  }
  //fd = open("/home/prog/openwrt/work/rb941-2nd-mtd-dump/mtdblock2.bin", O_RDONLY);
  //fd = open("./qqq.bin", O_RDONLY);
  fd = open("./qqq-nor.bin", O_RDONLY);
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
  //total_chunks = total_size / NOR_PAGE_SIZE(total_bytes_per_chunk);
  printf("I read %d bytes from data file! total_chunks = %d\n", total_size, total_chunks);
  //данные загрузили. работаем.
  for(chunkInNAND = 0; chunkInNAND < total_chunks; chunkInNAND++){
    addr = chunkToAddr(chunkInNAND);
    chunk = data + addr;
    pt = (void*)(chunk + total_bytes_per_chunk);
    x = (void*)pt;
    //x = chunk;
    //printf("%lu -->\n", x - data);
    //сначала преобразуем в наш endian
    pt_endianes_fix(pt);
    //распакуем. распаковка нужна так как могут быть дополнительные(extra) tags! запакованные в месте с нашими 4-мя полями(seq_number, ...)!
    memset(&t, 0x0, sizeof(t));
    yaffs_unpack_tags2_tags_only(&t, pt);
    printf("%u, %u: seq_number = %u, obj_id = %u, chunk_id = %u, n_bytes = %u, extra_available = %u\n", 
           chunkInNAND, addr, t.seq_number, t.obj_id,
           t.chunk_id, t.n_bytes, t.extra_available);
    if(t.extra_available){
      print_extra(&t);
    }
  }
end:
  free(data);
  return 0;
}
