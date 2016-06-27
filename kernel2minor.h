#ifndef __KERNEL2MINOR_H__
#define __KERNEL2MINOR_H__

#define PROGRAM_VERSION "0.22"

//печать сообщения только в случае активности флага verbose
#define verb_printf(args...) ({ if(verbose) printf(args); })

//выполняет обмен значениями между двумя переменными
#define exchg(a, b) { \
  typeof(a) tmp = a; \
  a = b;             \
  b = tmp;           \
}

//функции для безопасного добавления инфо переменной в info_block
#define define_ib_vars()                                                         \
  char *ib_ptr = (void*)info_block_buf;                                          \
  char ib_var_tmp_buf[INFO_BLOCK_VAR_LEN + 1];
#define __add_ib_var(val, maxlen, skip_snprintf)                                 \
  if(skip_snprintf != 1)                                                         \
    snprintf(ib_var_tmp_buf, sizeof(ib_var_tmp_buf), "%0"#maxlen"x", val);       \
  if((void*)ib_ptr + maxlen <= (void*)info_block_buf + info_block_size){         \
    memcpy(ib_ptr, ib_var_tmp_buf, maxlen);                                      \
    ib_ptr += maxlen;                                                            \
  }
#define _add_ib_var(val, maxlen, skip_snprintf) __add_ib_var(val, maxlen, skip_snprintf);
#define add_ib_var(val, tail...) _add_ib_var(val, INFO_BLOCK_VAR_LEN, (#tail[0] != '\0'));

#endif /* __KERNEL2MINOR_H__ */
