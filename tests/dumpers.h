#ifndef __DUMPERS_H__
#define __DUMPERS_H__

#include <string.h>

/* набод дамперов для отладки */

#define dumper(args...) _dumper(args, 0)
#define dumper_q(args...) _dumper(args, 1)
void _dumper(char *buf, int size, int quit){
  int a;
  if(!quit) printf("Dumping memory: 0x%p, size: %d\n", buf, size);
  for(a = 0; a < size; a++){
    if(a % 30 == 0 && a > 0) printf("\n");
    printf("%02x ", buf[a] & 0xFF);
  }
  if(!quit) printf("\n-----------------------------\n");
  else printf("\n");
}

void dumper_c(char *buf, int size){
  int a;
  printf("Dumping memory: 0x%p, size: %d\n", buf, size);
  for(a = 0; a < size; a++){
    if(a % 10 == 0) printf("\n");
    printf("%02x->'%c' ", buf[a] & 0xFF, buf[a]);
  }
  printf("\n-----------------------------\n");
}

void dumper4b(uint32_t *buf, int size){
  int a;
  printf("Dumping memory: 0x%p, size: %d\n", buf, size);
  for(a = 0; a < size / 4; a++){
    if(a % 8 == 0) printf("\n");
    printf("0x%08x, ", buf[a]);
  }
  printf("\n-----------------------------\n");
}

//uint32_t val binary dumper
#define printb(val) _printb(val, 0, 1);
#define printb_raw(val) _printb(val, 0, 0);
#define printbq(val) _printb(val, 1, 1);
void _printb(unsigned int val, int quiet, int bigend_convert) {
  unsigned char *p = (void*)&val, q;
  int a, b;
  char binoct[sizeof(unsigned char) * 8 + 1];
  if(!quiet) printf("Dumping uint32_t value := 0x%08X", val);
  if(!quiet)  printf("\n31----24-23----16-15-----8-7------0");
  if(!quiet)  printf("\n|      |.|      |.|      |.|      |\n");
  /* выводим биты в порядке big endian потому что битовые операции
     делаются компилятором именно в таком порядке и для их понимания так удобнее.
     если тебе нужно посмотреть реальное расположение битов в этой системе
     то используй *_raw функцию */
  if(bigend_convert) swap(val);
  for(a = 0; a < sizeof(val); a++){
    q = p[a];
    if(a > 0 && a < sizeof(val))
      printf(".");
    for(b = 0; b < sizeof(unsigned char) * 8; b++){
      snprintf(&binoct[b], 2, "%d", q & 0x1); q >>= 1;
    }
    //нужно вывести значения битов чиста в обратном порядке !
    for(b = strlen(binoct) - 1; b >= 0; b--)
      printf("%c", binoct[b]);
  }
  printf("\n");
  for(a = 0;  !quiet && a < sizeof(val); a++){
    printf("\\ 0x%02X /%c", p[a], a < sizeof(val) - 1 ? '.' : ' ');
  }
  if(!quiet) printf("\n-----------------------------------\n");
}

#endif //__DUMPERS_H__
