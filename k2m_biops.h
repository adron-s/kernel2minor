#ifndef __K2M_BIOPS_H__
#define __K2M_BIOPS_H__

#include <stdint.h>
#include <stdio.h>
#include <assert.h>

/*
                          библиотека бинарных операций
*/

//флаг необходимости конвертации порядка байт в big endian
extern int to_big_endian;
/* нужна ли конвертация в big endian byte order:
    we are BIG_ENDIAN system and flag IS_SET := 0
    we are BIG_ENDIAN system and flag not_set := 1
    we are LITTLE_ENDIAN system and flag IS_SET := 1
    we are LITTLE_ENDIAN system and flag not_set := 0 */
#define endian_need_conv (!!to_big_endian ^ (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))

/* конвертор из одного байтового порядка в обратный
   x обязательно должна быть переменной но не выражением !
*/
#define __swap(x, enc)                         \
({                                             \
  typeof(x) ret;                               \
  if(sizeof(x) == 1 || enc == 0)               \
    ret = x;                                   \
  else                                         \
    switch(sizeof(x)){                         \
      case 4:                                  \
        ret = ((((x) & 0x000000FF) << 24) |    \
               (((x) & 0x0000FF00) << 8 ) |    \
               (((x) & 0x00FF0000) >> 8 ) |    \
               (((x) & 0xFF000000) >> 24));    \
        break;                                 \
      case 2:                                  \
        ret = ((((x) & 0x00FF) << 8) |         \
              (((x) & 0xFF00) >> 8));          \
        break;                                 \
      default:                                 \
        ret = 0;                               \
        printf("Unsup len(%zu). var(%s)!\n",   \
               sizeof(x), #x);                 \
    };                                         \
  ret;                                         \
})
#define _swap(x) __swap(x, endian_need_conv)
//записать новое значение и выполнить перестаровку байтов если нужно
#define sswp(x, v) x = v; x = _swap(x);
#define swap(x) x = _swap(x);

//************************************************************************************
/* простые битовые операции */
#define BIT(n) (n < 32 ? (1 << (n)) : 0)
//создает битовую маску заданной длины
#define BIT_MASK(len) (BIT(len) - 1)
/* get_bit_value_mask - возвращает битовую маску
   для вставки значения val длиной len в позицию
   со здвигом слева := loff бит
   ! эта функция не учитывает байтовый порядок !
   смотри как я это решаю в макросе set_be_bfv
*/
static uint32_t __attribute__((optimize("O0"))) get_bvm(int loff, int len, uint32_t val){
  uint32_t ret = val & BIT_MASK(len);
  uint32_t shift = 32 - (loff + len);
  if(shift > 0 && shift < 32)
    ret <<= shift;
  return ret;
}//-----------------------------------------------------------------------------------

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
static int __attribute__((optimize("O0")))
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
  //странные глюки нам ненужны. возвращаем ошибку.
  if(*offset < 0 || *size < 0) return -1;
  //all ok
  return block_4b_n;
}//-----------------------------------------------------------------------------------


//************************************************************************************
/* функции для чтения и записи полей bitfield структуры.
   если установлен флаг endian_need_conv то происходит
     переконвертация в необходимый endian 

*/
/* извлекает значение битовой переменной из массива
   байт(представленного в формате !!! big endian !!!)
   в виде little_endian !*/
#define get_be_bfv(parent, var)({              \
  uint32_t _ret;                               \
  if(endian_need_conv){                        \
    int block_4b_n, offset, size;              \
    assert(bitset_mega_calc((parent), var,     \
      block_4b_n, offset, size) >= 0);         \
    _ret = ((uint32_t*)&(parent))[block_4b_n]; \
    _ret = __swap(_ret, 1);                    \
    _ret <<= offset;                           \
    _ret >>= 32 - size;                        \
  }else{                                       \
   /* конвертация для данной системы */        \
   /* не требуется. видимо для нее порядок */  \
   /* битой и так является родным */           \
    _ret = parent.var;                         \
  }                                            \
  (_ret);                                      \
})
/* устанавливает значение битовой переменной ...
   аналогична предидущей функции в этом if блоке
*/
#define set_be_bfv(parent, var, val)({         \
  if(endian_need_conv){                        \
    int block_4b_n, offset, size;              \
    uint32_t *t;                               \
    assert(bitset_mega_calc((parent), var,     \
      block_4b_n, offset, size) >= 0);         \
    t = &((uint32_t*)&(parent))[block_4b_n];   \
    /* битовые операции мы должны */           \
    /* проводить в родном для этой */          \
    /* системы байтовом порядке !*/            \
    uint32_t q = __swap(*t, 1);                \
    /* чистим место для вставки val */         \
    q &= ~get_bvm(offset, size, 0xFFFFFFFF);   \
    /* записываем val */                       \
    q |= get_bvm(offset, size, val);           \
    /* с битовыми операциями завершили. */     \
    /* конвертируем в big endian */            \
    *t = __swap(q, 1);                         \
  }else{                                       \
    /* конвертация не требуется. ... */        \
    parent.var = val;                          \
  }                                            \
})
//-----------------------------------------------------------------------------------

#endif /* __K2M_BIOPS_H__ */
