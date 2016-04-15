/*
 *  Packs OpenWrt Kernel to Mikrotik's version of yaffs2 file system
 *
 *  Copyright (C) 2016 Sergey Sergeev <sergeev.sergey@yapic.net>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

/* прикидываемся yaffs_utils
все нужные файлы yaffs2 бери из его транка из папки utils! там есть различия.
часть файлов нужно брать из direct папки. в папке utils это видно по симлинкам! */
#define CONFIG_YAFFS_UTIL 1
#define CONFIG_YAFFS_DEFINES_TYPES 1
#include "yaffs2/yaffs_guts.h"
#include "yaffs2/yaffs_packedtags2.h"
//скопировал с yaffs_packedtags2 для упаковки данных в extra tags
#define EXTRA_HEADER_INFO_FLAG 0x80000000
#define EXTRA_OBJECT_TYPE_SHIFT (28)
#define EXTRA_OBJECT_TYPE_MASK  ((0x0f) << EXTRA_OBJECT_TYPE_SHIFT)

//имя файла ядра в файловой система yaffs2
#define KERNEl_YAFFS_FILE_NAME "kernel"
/* размер блока(в терминах yaffs2. A Group of chunks(a block is the unit of erasure). Все страницы(чанки) блока имеют одинаковый sequence number!
   все это нужно именно из за того что мы можем стирать данные только одним блоком! так что нужно знать какие чанки принадлежат этому блоку. */
#define NOR_BLOCK_SIZE 65536
//размер oob области(spare size, tags). ecc не используется!
#define NOR_OOB_SIZE 16
/* размер области полезных данных чанки но без oob!
  так же это называют страницей памяти(page size in block) */
#define NOR_CHUNK_DATA_SIZE 1024
//размер всей чанки (данные чанки + oob).
#define NOR_CHUNK_FULL_SIZE (NOR_CHUNK_DATA_SIZE + NOR_OOB_SIZE)
//сколько чанков(data + oob) в блоке
#define CHUNKS_PER_BLOCK (NOR_BLOCK_SIZE / NOR_CHUNK_FULL_SIZE)

char kernel_file[255];
char res_file[255];
int endians_need_conv = 0;
int verbose = 0;

struct stat kernel_file_stats;

//печать сообщения только в случае активности флага verbose
#define verb_printf(args...) ({ if(verbose) printf(args); })

/* конвертор из одного байтового порядка в обратный
   x обязательно должна быть переменной но не выражением !
*/
#define swap(x)                                \
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
#define sswp(x, v) x = v; x = swap(x);

//************************************************************************************
void print_help(void){
  int a;
  char *usage[] = 
    { "-k", "Path to kernel file", kernel_file,
      "-r", "Path to result file", res_file,
      "-e", "Enable endians convert", endians_need_conv ? "Yes" : "No",
      "-v", "Verbose output", verbose ? "Yes" : "No",
      "-h", "Show help and exit", "" };
  printf("Usage:\n");
  for(a = 0; a < sizeof(usage) / sizeof(usage[0]); a += 3){
    printf("  %-5s%-25s%s\n", usage[a], usage[a + 1], usage[a + 2]);
  }
}//-----------------------------------------------------------------------------------

//************************************************************************************
//выполняет заполнение tags(spare data из oob - информация используемая yaffs для идентификации чанков)
//offset это смещение в buf с которого начинается oob(yaffs2 tags)
void cook_tags(char *buf, unsigned int offset, unsigned int obj_id, unsigned int seq_number, unsigned int chunk_id, unsigned int n_bytes){
  struct yaffs_ext_tags t;
  struct yaffs_packed_tags2_tags_only *ptt = (void*)(buf + offset); //packed tags2 tags only(without ecc!)
  assert(sizeof(*ptt) == NOR_OOB_SIZE); //защита от дурака
  /* так как у нас тип объекта == файл то я упростил функцию yaffs_pack_tags2_tags_only до вот этого кода!
     паковка необходимых для extra tags данных в поля структуры ptt это уже обязанность того кто нас вызывает
     раз уж он так хочет использовать extra tags */
  memset(ptt, 0x0, sizeof(*ptt));
  sswp(ptt->chunk_id, chunk_id);
  sswp(ptt->n_bytes, n_bytes);
  sswp(ptt->obj_id, obj_id);
  sswp(ptt->seq_number, seq_number);
}//-----------------------------------------------------------------------------------

//************************************************************************************
/* заполняет заголовок для объекта
  обязательно забей память 0xff перед вызовом этой функции! */
void cook_object_header(char *buf, char *name){
  struct yaffs_obj_hdr *oh = (struct yaffs_obj_hdr *)buf;
  struct stat *s = &kernel_file_stats;
  sswp(oh->type, YAFFS_OBJECT_TYPE_FILE);
  sswp(oh->parent_obj_id, YAFFS_OBJECTID_ROOT);
  memset(oh->name, 0, sizeof(oh->name));
  strncpy(oh->name, name, YAFFS_MAX_NAME_LENGTH);
  sswp(oh->yst_mode, s->st_mode);
  sswp(oh->yst_uid, s->st_uid);
  sswp(oh->yst_gid, s->st_gid);
  sswp(oh->yst_atime, s->st_atime);
  sswp(oh->yst_mtime, s->st_mtime);
  sswp(oh->yst_ctime, s->st_ctime);
  sswp(oh->yst_rdev, s->st_rdev);
  sswp(oh->file_size_low , s->st_size);
  sswp(oh->file_size_high, s->st_size >> 32);
}//-----------------------------------------------------------------------------------

//************************************************************************************
/* осуществляет заполнение заголовка для объекта(0-й чанки) и запись чанки в файл результата(r) */
int fill_and_write_obj_header(int r, int obj_id, int seq_number, char *buf, int buf_size, int *n){
  int len;
  int extra_obj_type, extra_parent_id;
  int chunk_id = 0; //для obj header номер чанки == 0
  int n_bytes = 0; //для чанки содержащей заголовок объекта n_bytes всегда == 0
  //подготовим буфер к заполнению
  memset(buf, 0xff, buf_size);
  verb_printf("%u: Writing chunk = 0 for obj(%u) HEADER, seq = %u\n", (*n)++, obj_id, seq_number);
  //заполним заголовок объекта
  cook_object_header(buf, KERNEl_YAFFS_FILE_NAME);
  /* запакуем ext tags в поля obj_id и chunk_od. смотри ф-ю yaffs_pack_tags2_tags_only !
     так же упрощаем алгоритм полагая что: extra_is_shrink == 0, extra_shadow == 0,
     extra_obj_type == файл, extra_file_size == 0(так было во всех дампах что я анализировал)
  */
  //obj_id
  extra_obj_type = YAFFS_OBJECT_TYPE_FILE; //тип объекта - файл
  obj_id &= ~EXTRA_OBJECT_TYPE_MASK;
  obj_id |= (extra_obj_type << EXTRA_OBJECT_TYPE_SHIFT);
  //chunk_id
  extra_parent_id = YAFFS_OBJECTID_ROOT; //id родителя. наш файл оежит в корне.
  chunk_id = EXTRA_HEADER_INFO_FLAG | extra_parent_id;
  //заполним данные tags(oob)
  cook_tags(buf, NOR_CHUNK_DATA_SIZE, obj_id, seq_number, chunk_id, n_bytes);
  len = write(r, buf, buf_size);
  if(len != buf_size){
    fprintf(stderr, "Can't write obj header %u chunk to result file! ", obj_id);
    perror("error descr");
    return 0;
  }
  return len; //вернем сколько байт записали
}//-----------------------------------------------------------------------------------

//************************************************************************************
//осуществляет запаковку файла ядра(k) в файл yaffs2(r)
void do_pack(int k, int r){
  int obj_id = YAFFS_NOBJECT_BUCKETS + 1;
  int seq_number = YAFFS_LOWEST_SEQUENCE_NUMBER;
  int chunk_id = 0;
  char buf[NOR_CHUNK_FULL_SIZE]; //буфер в котором мы будем собирать чанку. он состоит из данных + tags(oob, metadata)
  char hole_fill_buf[sizeof(buf)]; //буфер для заполнения дыр
  unsigned int hole_size; //для вымеривания дырок в конце блока
  int rlen, wrlen;
  int total_bwr_len = 0;
  int n = 0; //для подсчета сколько чанков было всего записано(заполнители дыр сюда не входят!)
  int bc = 0; //для подсчета сколько блоков было записано
  int wrbc = 0; //для подсчета сколько всего было записано байт
  //заполним буфер для дыр
  memset(hole_fill_buf, 0xff, sizeof(hole_fill_buf));

  //заполним заголовочную чанку и запишем ее в r
  wrlen = fill_and_write_obj_header(r, obj_id, seq_number, buf, sizeof(buf), &n);
  wrbc += wrlen;
  total_bwr_len += wrlen; //считаем сколько байт блока мы уже записали

  //подготовим буфер к заполнению
  memset(buf, 0xff, sizeof(buf));
  chunk_id++; //переходим к следующей чанке. это уже будет чанка с данными.
  //читаем из файла ядра чанками размером NOR_CHUNK_DATA_SIZE
  while((rlen = read(k, buf, NOR_CHUNK_DATA_SIZE)) > 0){
    //если места в блоке уже не осталось чтобы вместить текущую чанку
    if(total_bwr_len + sizeof(buf) > NOR_BLOCK_SIZE){
      hole_size = NOR_BLOCK_SIZE - total_bwr_len; //рассчитаем размер дыры в хвосте блока
      verb_printf("Writing hole filler: seq = %u, offset = %u, hole_size = %u\n",  seq_number, total_bwr_len, hole_size);
      if((wrlen = write(r, hole_fill_buf, hole_size)) != hole_size){
        fprintf(stderr, "Can't write hole fill data %u: %u to result file! ", obj_id, seq_number);
        perror("error descr");
        return;
      }
      //начнем использование нового блока
      seq_number++;
      total_bwr_len = 0;
      bc++;
      wrbc += wrlen;
    }
    //пищем текущую чанку в файл r
    verb_printf("%u: Writing chunk = %u for obj(%u), seq = %u, data len = %u, chunk_len = %zu\n", n++, chunk_id, obj_id, seq_number, rlen, sizeof(buf));
    //рассчитаем yaffs2 tags для текущей чанки
    cook_tags(buf, NOR_CHUNK_DATA_SIZE, obj_id, seq_number, chunk_id++, rlen);
    if((wrlen = write(r, buf, sizeof(buf))) != sizeof(buf)){
      fprintf(stderr, "Can't write data chunk %u: %u->%u to result file! ", obj_id, seq_number, chunk_id);
      perror("error descr");
      return;
    }
    wrbc += wrlen; //считаем сколько всего было записано байт
    total_bwr_len += wrlen; //считаем сколько байт блока мы уже записали
    //подготовим буфер к заполнению(уже для следующей чанки)
    memset(buf, 0xff, sizeof(buf));
  }

  //данные записали. осталось заполнить оставшееся до конца блока место
  hole_size = NOR_BLOCK_SIZE - total_bwr_len; //рассчитаем размер огромной дыры в хвосте блока
  verb_printf("All data has been writen. But to the end of the block we left %u bytes. Lets fill it.\n", hole_size);
  while(hole_size > 0){
    rlen = hole_size > sizeof(hole_fill_buf) ? sizeof(hole_fill_buf) : hole_size; //сколько нужно записать данные в этом проходе цикла
    if(rlen == sizeof(hole_fill_buf)) verb_printf("%u: ", n++); else verb_printf("LAST: ");
    verb_printf("Writing hole fill block: seq = %u, offset = %u, left_hole_size = %u, len = %u\n", seq_number, total_bwr_len, hole_size, rlen);
    if((wrlen = write(r, hole_fill_buf, rlen)) != rlen){
      fprintf(stderr, "Can't write hole fill data %u: %u to result file! ", obj_id, seq_number);
      perror("error descr");
      return;
    }
    wrbc += wrlen;
    total_bwr_len += wrlen;
    hole_size -= wrlen;
  }
  bc++;
  verb_printf("\n");
  //последняя проверка. вдруг гдето ошиблись.
  if(wrbc % NOR_BLOCK_SIZE != 0 && wrbc / NOR_BLOCK_SIZE != bc){
    printf("Warning! Something went wrong!\n");
  }
  //выведем статистику по проделанной работе.
  printf("Successfully writed %u blocks and %u bytes\n", bc, wrbc);
  printf("Each block contain %u chanks + %u bytes tail hole.\n", CHUNKS_PER_BLOCK, NOR_BLOCK_SIZE - CHUNKS_PER_BLOCK * NOR_CHUNK_FULL_SIZE);
  printf("Each chunk(%u bytes) consists: data part(%u bytes) + tags part(%u bytes).\n", NOR_CHUNK_FULL_SIZE, NOR_CHUNK_DATA_SIZE, NOR_OOB_SIZE);
}//-----------------------------------------------------------------------------------

//************************************************************************************
int main(int argc, char *argv[]){
  int k;
  int r;
  int ch; //для парсинга параметров
  //загружаем параметры командной строки
  while( (ch = getopt(argc, argv, "k:r:evh")) != -1){
    switch(ch){
      case 'k': snprintf(kernel_file, sizeof(kernel_file) - 1, "%s", optarg); break;
      case 'r': snprintf(res_file, sizeof(res_file) - 1, "%s", optarg); break;
      case 'e': endians_need_conv = 1; break;
      case 'v': verbose = 1; break;
      case 'h': print_help(); exit(0); break;
    }
  }
  if(!*kernel_file || !*res_file){
    print_help(); exit(0);
  }
  //получим данные lstats(mode, uid, guid, ctime, ...) файла ядра
  memset(&kernel_file_stats, 0x0, sizeof(kernel_file_stats));
  if(lstat(kernel_file, &kernel_file_stats) < 0){
    perror("Can't get lstat from kernel file!");
    exit(-1);
  }
  k = open(kernel_file, O_RDONLY);
  if(k <= 0){
    perror("Can't open kernel file");
    exit(-1);
  }
  r = creat(res_file, 0);
  if(r <= 0){
    perror("Can't create result file");
    close(k);
    exit(-1);
  }
  do_pack(k, r);
  close(k);
  close(r);
  return 0;
}//-----------------------------------------------------------------------------------
