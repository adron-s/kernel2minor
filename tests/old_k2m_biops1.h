#ifndef __K2M_BIOPS_H__
#define __K2M_BIOPS_H__

#include <stdint.h>
#include <stdio.h>

//нужна ли конвертация. устанавливавется автоматически в 1 для не big_endian систем
static int endian_need_conv = __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__;

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

//возвращает размер битовой переменной(bit var size)
#define bvs(parent, var)({                     \
  int ret;                                     \
  typeof(parent) x;                            \
  memset(&x, 0xFF, sizeof(x));                 \
  ret = ffs(~x.var) - 1;                       \
  if(ret == -1) ret = 32;                      \
  if(ret < 0 || ret > 32) ret = -1;            \
  ret;                                         \
})

/* возвращает смещение(в битах) от начала 4-х
     байтового блока и до первого бита
     переменной:
   [xxxBITVARDATAyyyyyy] = 3
*/
#define bvbb(parent, var)({                    \
  typeof(parent) x;                            \
  uint32_t *o = get_bf_4b_oct_ptr(x, var);     \
  *o = 0xFFFFFFFF;                             \
  x.var = 0;                                   \
  (ffs(~get_unopt_val(o)) - 1);                \
})

/* возвращает номер 4-х байтового октета
   в котором расположилась переменная var */
#define get_bf_4b_oct(parent, var)({           \
  int ret = -1;                                \
  typeof(parent) x;                            \
  uint32_t *p = (void*)&x;                     \
  int a;                                       \
  memset(&x, 0xFF, sizeof(x));                 \
  x.var = 0;                                   \
  for(a = 0; a < sizeof(x) / 4; p++, a++)      \
    if((*p) != 0xFFFFFFFF){                    \
      ret = a;                                 \
      break;                                   \
    }                                          \
  ret;                                         \
})
/* возвращает указатель (uint32_t *) на
   4-х байтовый блок в котором назодится
   тело переменной var */
#define get_bf_4b_oct_ptr(parent, var)         \
  (((uint32_t*)&parent) +                      \
  get_bf_4b_oct(parent, var))

/* извлекает значение битовой переменной из массива
   байт(представленного в формате !!! big endian !!!)
   в виде little_endian !*/
#define get_be_bfv(parent, var)({              \
  uint32_t _ret;                               \
  if(endian_need_conv){                        \
    _ret = (                                   \
      *get_bf_4b_oct_ptr(parent, var));        \
    _ret = __swap(_ret, 1);                    \
    _ret <<= bvbb(parent, var);                \
    _ret >>= 32 - bvs(parent, var);            \
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
    uint32_t *t = (                            \
      get_bf_4b_oct_ptr(parent, var));         \
    /* битовые операции мы должны */           \
    /* проводить в родном для этой */          \
    /* системы байтовом порядке !*/            \
    uint32_t q = __swap(*t, 1);                \
    int offset = bvbb(parent, var);            \
    int size = bvs(parent, var);               \
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

#define BIT(n) (n < 32 ? (1 << (n)) : 0)
//создает битовую маску заданной длины
#define BIT_MASK(len) (BIT(len) - 1)

/* get_bit_value_mask - возвращает битовую маску
   для вставки значения val длиной len в позицию
   со здвигом слева := loff бит
   ! эта функция не учитывает байтовый порядок !
   смотри как я это решаю в макросе set_be_bfv
*/
static inline uint32_t get_bvm(int loff, int len, uint32_t val){
  uint32_t ret = val & BIT_MASK(len);
  uint32_t shift = 32 - (loff + len);
  if(shift > 0 && shift < 32)
    ret <<= shift;
  return ret;
}

//злобная борьба с оптимизацией gcc. чудные вещи начинают происходить с макросами при -O2 !
static uint32_t __attribute__((optimize("O0"))) get_unopt_val(uint32_t *v){ return *v; }

#endif /* __K2M_BIOPS_H__ */
