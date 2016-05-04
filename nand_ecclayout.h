#ifndef __NAND_ECCLAYOUT_H__
#define __NAND_ECCLAYOUT_H__

#define MTD_MAX_OOBFREE_ENTRIES 8

/* эти структуры взяты с проекта yaffs2utils. ищи его на гитхабе. */

struct nand_oobfree {
  unsigned offset;
  unsigned length;
};

struct nand_ecclayout_user {
  unsigned eccbytes;
  unsigned eccpos[64];
  unsigned oobavail;
  struct nand_oobfree oobfree[MTD_MAX_OOBFREE_ENTRIES];
};

typedef struct nand_ecclayout_user nand_ecclayout_t;

/* ecc layout используемый в NAND флешке RB750/RB751:
     block size := 2048 bytes
     eraseblock := 131072 bytes
     oob := 64 bytes
*/
static nand_ecclayout_t nand_oob_64 = {
  .eccbytes = 24,
  .eccpos = { 40, 41, 42, 43, 44, 45, 46, 47,
              48, 49, 50, 51, 52, 53, 54, 55,
              56, 57, 58, 59, 60, 61, 62, 63 },
  .oobfree = {{ .offset = 2, .length = 38 }},
};

/* пустой ecc layout - заглушка для случая когда
   ecc не используется(например NOR flash)
*/
static nand_ecclayout_t nand_oob_0 = {
  .eccbytes = 0,
  .eccpos = { },
  .oobfree = {{ .offset = 0, .length = 0 }},
};

//************************************************************************************
/* возвращает ecc layout исходя из переданного размера чанки */
nand_ecclayout_t get_ecclayout_by_chunk_size(int chunk_size){
  switch(chunk_size){
    case 2048:
      return nand_oob_64;
    default:
      return nand_oob_0;
  }
}//-----------------------------------------------------------------------------------

#endif /* __NAND_ECCLAYOUT_H__ */
