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


#include "yaffs_list.h"
#include "yaffs_guts.h"
#include "yaffs_packedtags2.h"

// This one is easier, since the types are more standard. No funky shifts here.
void object_header_little_to_big_endian(struct yaffs_obj_hdr* oh)
{
    oh->type = SWAP32(oh->type); // GCC makes enums 32 bits.
    oh->parent_obj_id = SWAP32(oh->parent_obj_id); // int
    oh->sum_no_longer_used = SWAP16(oh->sum_no_longer_used); // u16 - Not used, but done for completeness.
    // name = skip. Char array. Not swapped.
    oh->yst_mode = SWAP32(oh->yst_mode);

    // Regular POSIX.
    oh->yst_uid = SWAP32(oh->yst_uid);
    oh->yst_gid = SWAP32(oh->yst_gid);
    oh->yst_atime = SWAP32(oh->yst_atime);
    oh->yst_mtime = SWAP32(oh->yst_mtime);
    oh->yst_ctime = SWAP32(oh->yst_ctime);

    oh->file_size_low = SWAP32(oh->file_size_low); // Aiee. An int... signed, at that!
    oh->file_size_high = SWAP32(oh->file_size_high); // Aiee. An int... signed, at that!
    oh->equiv_id = SWAP32(oh->equiv_id);
    // alias  - char array.
    oh->yst_rdev = SWAP32(oh->yst_rdev);

    oh->win_ctime[0] = SWAP32(oh->win_ctime[0]);
    oh->win_ctime[1] = SWAP32(oh->win_ctime[1]);
    oh->win_atime[0] = SWAP32(oh->win_atime[0]);
    oh->win_atime[1] = SWAP32(oh->win_atime[1]);
    oh->win_mtime[0] = SWAP32(oh->win_mtime[0]);
    oh->win_mtime[1] = SWAP32(oh->win_mtime[1]);

    oh->reserved[0] = SWAP32(oh->reserved[0]);
    oh->reserved[1] = SWAP32(oh->reserved[1]);

    oh->inband_shadowed_obj_id = SWAP32(oh->inband_shadowed_obj_id);
    oh->inband_is_shrink = SWAP32(oh->inband_is_shrink);
    oh->shadows_obj = SWAP32(oh->shadows_obj);
    oh->is_shrink = SWAP32(oh->is_shrink);
}


void pt_endianes_fix(struct yaffs_packed_tags2_tags_only *pt){
  pt->seq_number = SWAP32(pt->seq_number);
  pt->obj_id = SWAP32(pt->obj_id);
  pt->chunk_id = SWAP32(pt->chunk_id);
  pt->n_bytes = SWAP32(pt->n_bytes);
}

#define NOR_OOB_SIZE 16
#define NOR_PAGE_SIZE(x) (x + NOR_OOB_SIZE)

#define erasesize 65536 //4096
#define total_bytes_per_chunk 1024
#define chunks_per_block (erasesize / (total_bytes_per_chunk + 16))

unsigned int chunkToAddr(int chunkInNAND){
/*  return 65535 + chunkInNAND * NOR_PAGE_SIZE(total_bytes_per_chunk); */
  return erasesize * (chunkInNAND / chunks_per_block)
                + NOR_PAGE_SIZE(total_bytes_per_chunk)
                * (chunkInNAND % chunks_per_block);
}

int main(void){
  int fd;
  int fd2;
  char *data;
  char *chunk;
  unsigned int total_size = 0;
  unsigned int total_chunks = 0;
  struct yaffs_packed_tags2_tags_only *pt = NULL; // 16 bytes
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
  fd = open("/home/prog/openwrt/work/rb941-2nd-mtd-dump/mtdblock1.bin", O_RDONLY);
  fd2 = open("/home/prog/openwrt/work/rb941-2nd-mtd-dump/mtdblock1_unfrag.bin", O_WRONLY | O_CREAT);
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
  total_chunks = total_size / erasesize * chunks_per_block;
  //total_chunks = total_size / NOR_PAGE_SIZE(total_bytes_per_chunk);
  printf("I read %d bytes from data file! total_chunks = %d\n", total_size, total_chunks);
  //данные загрузили. работаем.
  for(chunkInNAND = 0; chunkInNAND < total_chunks; chunkInNAND++){
    addr = chunkToAddr(chunkInNAND);
    chunk = data + addr;
    write(fd2, chunk, 1040);
    pt = (void*)(chunk + total_bytes_per_chunk);
    x = (void*)pt;
    //if(chunkInNAND % chunks_per_block == 0 && chunkInNAND > 0) printf(".\n");
    //if(SWAP32(pt->seq_number) == 0 || SWAP32(pt->seq_number) == 0xFFFFFFFF){ continue; }
    printf("%u: seq_number = %u, obj_id = %u, chunk_id = %u, n_bytes = %u\n", 
           addr, SWAP32(pt->seq_number), SWAP32(pt->obj_id),
           SWAP32(pt->chunk_id), SWAP32(pt->n_bytes));
    //printf("%8u: ", addr); for(a = 0; a < 16; a++){ printf("%3x", x[a] & 0xFF); } printf("\n");
  }
  close(fd2);
end:
  free(data);
  return 0;
}
