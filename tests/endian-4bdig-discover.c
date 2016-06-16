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
#include "k2m_biops.h"
#include "dumpers.h"

typedef uint8_t u8;
typedef uint32_t u32;

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


struct x51 {
  uint32_t p1:1;
  uint32_t p2:10;
  uint32_t p3:3;
  uint32_t p4:18;
};

int main(void){
  uint32_t val = 0xFFFFFFFF;
  int a;
  struct x51 *vx = (void*)&val;
  vx->p1 = 0;
  printb(val);
  for(a = 0; a < 32; a++){
//    printbq(val);
    if(a % 8 == 0) printf("\n0x%x\n", __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ ? (val >> 24) & 0xFF : val & 0xFF);
    printf("%d: %X\n", a, last_bit_ret_and_endian_shift(val));
  }
}
