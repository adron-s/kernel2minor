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
#include "dumpers.h"

typedef uint8_t u8;
typedef uint32_t u32;

#define CONFIG_YAFFS_DEFINES_TYPES 1
#define CONFIG_YAFFS_UTIL 1
#define YCHAR char
#define YUCHAR unsigned char 
#include "yaffs_guts.h"
#include "yaffs_packedtags1.h"
#include "yaffs_tagscompat.h"
#include "yaffs_ecc.h"
#include "k2m_biops.h"

unsigned char x25[ ] = { 0x00, 0x00, 0x04, 0x00, 0x00, 0x40, 0x40, 0x03  };

//************************************************************************************
/* выссчитывает для parent.var слещующие значения:
     block_4b_n - номер 4-х байтового блока начиная с 0 в котором располагается переменная
     offset - смещение в битах от начала 4-х байтового блока и до первого бита переменной
     size - размер переменной в битах */
#define bitset_mega_calc(parent, var, block_4b_n, offset, size)  ({ \
  typeof(parent) x;                                                 \
  memset((void*)&x, 0xFF, sizeof(x));                               \
  x.var = 0;                                                        \
  block_4b_n = _bitset_megacalc((void*)&x, sizeof(x),               \
                          &offset, &size);                          \
  block_4b_n;                                                       \
})
/* возвращает старший(левый) бит числа val
   !в независимости от endian-а системы!
   а также  выполняет побитовый сдвиг
   val на 1 в лево для big endian систем и
   вправо на 1 для little endian систем */
#define last_bit_ret_and_endian_shift(val)({  \
  int ret;                                    \
  /* будем совместимы с обоими endianЦами */  \
  if(__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__){ \
    ret = val & 0x80000000;                   \
    val <<= 1;                                \
  }else{                                      \
    ret = val & 0x1;                          \
    val >>= 1;                                \
  }                                           \
  ret != 0;                                   \
})
/* выключаем оптимизацию чтобы не ловитиь чудные глюки при -O2 */
int __attribute__((optimize("O0")))
_bitset_megacalc (void *x, int x_size, int *offset, int *size){
  uint32_t val = 0;
  int a, block_4b_n = -1;
  //мы работаем только с выровняными по смещению 32 бита bitset структурами
  assert(x_size % sizeof(uint32_t) == 0);
  //найдем номен 32-х битного блока в котором распологается битовая переменная
  for(a = 0; a < x_size / sizeof(uint32_t); a++){
    val = ((uint32_t *)x)[a];
    if(val != 0xFFFFFFFF){
      block_4b_n = a;
      break;
    }
  }
  //не нашли. очень очень странно! вернем ошибку.
  if(block_4b_n < 0) return -1;
  //теперь ищем смещение и размер битовой переменной
  *offset = -1; *size = -1;
  for(a = 0; a < sizeof(uint32_t) * 8; a++){
    //будем совместимы с обоими endianЦами
    if(!last_bit_ret_and_endian_shift(val)){
      if(*offset == -1) *offset = a;
      *size = a - *offset + 1;
    }
  }
  //странные глюки нам не нужны. возвращаем ошибку.
  if(*offset < 0 || *size < 0) return -1;
  //all ok
  return block_4b_n;
}//-----------------------------------------------------------------------------------

#define xaox1(p1, var, args...)                 \
  assert(bitset_mega_calc(p1, var, args) >= 0); \
  printf("%-18s 4b_block_n := %-5d offset := %-5d size := %d\n", #var, p, offset, size);

int main(void){
  struct yaffs_packed_tags1 x1;
  struct yaffs_tags *tags1 = (void*)&x1;
  int p = -1;
  int offset = 0, size = 0;
  xaox1((*tags1), chunk_id, p, offset, size);
  xaox1((*tags1), serial_number, p, offset, size);
  xaox1((*tags1), n_bytes_lsb, p, offset, size);
  xaox1((*tags1), obj_id, p, offset, size);
  xaox1((*tags1), ecc, p, offset, size);
  xaox1((*tags1), n_bytes_msb, p, offset, size);
/*  printf("%u, %u, %u, 0x%x\n", get_be_bfv(x1, chunk_id), get_be_bfv(x1, obj_id),
         get_be_bfv(x1, n_bytes), get_be_bfv(x1, ecc)); */
  return 0;
}
