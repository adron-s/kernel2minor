#ifndef __KERNEL2MINOR_H__
#define __KERNEL2MINOR_H__

//печать сообщения только в случае активности флага verbose
#define verb_printf(args...) ({ if(verbose) printf(args); })

/* конвертор из одного байтового порядка в обратный
   x обязательно должна быть переменной но не выражением !
*/
#define _swap(x)                               \
({                                             \
  typeof(x) ret;                               \
  if(sizeof(x) == 1 || endians_need_conv == 0) \
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
});
//записать новое значение и выполнить перестаровку байтов если нужно
#define sswp(x, v) x = v; x = _swap(x);
#define swap(x) x = _swap(x);

//выполняет обмен значениями между двумя переменными
#define exchg(a, b) { \
  typeof(a) tmp = a; \
  a = b;             \
  b = tmp;           \
}


#endif /* __KERNEL2MINOR_H__ */
