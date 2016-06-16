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

/* ecc layout используемый в NAND флешках RB4xx:
     block size := 512 bytes
     eraseblock := 16384 bytes
     oob := 16 bytes
   ! этот layout отличается от обычного для 512 блока layout-а !
   ! тут расположение free oob весьма странное !
*/
static nand_ecclayout_t rb4xx_nand_oob_16 = {
  .eccbytes = 6,
  .eccpos = {8, 9, 10, 13, 14, 15},
  .oobfree = { { 0, 4 }, { 6, 2 }, { 11, 2 }, { 4, 2 } },
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
    case 512:
      return rb4xx_nand_oob_16;
    default:
      return nand_oob_0;
  }
}//-----------------------------------------------------------------------------------

//************************************************************************************
/* возвращает размер oobfree(сумму длин всех блоков указанных в oobfree) */
int get_oobfree_len(struct nand_oobfree *oobfree){
  int res = 0;
  int len, a;
  for(a = 0; a < MTD_MAX_OOBFREE_ENTRIES; a++){
    len = oobfree[a].length;
    if(len <= 0) break; //чуть что сразу выходим
    res += len;
  }
  return res;
}//-----------------------------------------------------------------------------------

//************************************************************************************
/* возвращает максимальное смещение указанное в ecc_layout-е(в eccpos или oobfree)
   я использую эту функцию для рассчета общего размера oob
*/
int get_ecc_layout_max_offset(nand_ecclayout_t *l){
  int max_off = 0, a, off;
  //ищем max_off среди oobfree
  for(a = 0; a < MTD_MAX_OOBFREE_ENTRIES; a++){
    off = l->oobfree[a].length + l->oobfree[a].offset;
    if(off <= 0) break; //чуть что сразу выходим
    if(max_off < off)
      max_off = off;
  }
  //ищем max_off среди eccpos
  for(a = 0; a < l->eccbytes; a++){
    off = l->eccpos[a];
    if(max_off < off)
      max_off = off;
  }
  return max_off;
}//-----------------------------------------------------------------------------------

#endif /* __NAND_ECCLAYOUT_H__ */
