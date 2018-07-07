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
#include "yaffs2/yaffs_guts.h"
#include "yaffs2/yaffs_packedtags1.h"
#include "yaffs2/yaffs_packedtags2.h"
#include "yaffs2/yaffs_tagscompat.h"

#include "kernel2minor.h"
#include "k2m_biops.h"
#include "nand_ecclayout.h"

//#include "tests/dumpers.h"

//флаг необходимости конвертации порядка байт в big endian
int to_big_endian = 0;
//размер блока с инфо данными о созданном образе. рассчитывается в calc_needed_vars
static int info_block_size = 0;
//длина строки переменной инфо блока(без '\0'. он там не используется)
#define INFO_BLOCK_VAR_LEN 8
//имя файла ядра в файловой система yaffs2
#define KERNEl_YAFFS_FILE_NAME "kernel"
/* размер блока данных по которому считается ECC. для yaffs2 это 256 */
#define ECC_BLOCK_SIZE 256

char *info_block_buf = NULL;
char kernel_file[255];
char res_file[255];
struct stat kernel_file_stats;
nand_ecclayout_t ecc_layout;
//имя платформы. параметр -p. для инфо блока.
char platform_name[INFO_BLOCK_VAR_LEN + 1];

//значения по умолчанию для наших параметров
int chunk_size = 1024; //по умолчанию == размеру для NOR флешки
int use_ecc = 0; //используется ли ECC(если нет ecc то нет и oob_layout-а и tags пишутся линейно сразу после data)
int verbose = 0; //говорливость в stdout
//добавить к образу блок с данными описывающими его параметры(размер, blocksize, chunksize, etc...)
int add_image_info_block = 0; //это нужно для образов используемых перепрошивальщиком openwrt(для nand флешей)
int align_size = 0; //нужно для openwrt-шного sysupgrade-а. размер блока который будет добавлен к нам сторонним скриптом(sysupgrade-ом)

//параметры для создаваемой нами файловой системы yaffs2. рассчитываются ф-ей calc_needed_vars.
static int chunk_data_size = 0;
static int chunk_full_size = 0;
static int chunk_oob_total_size = 0; //общий размер oob(data ecc + !ДЫРЫ! + oobfree )
static int chunk_oob_free_size = 0; //размер oobfree в oob(место для tags)
static int block_size = 0;
static int chunks_per_block = 0;
static int yaffs_version = 2;

//************************************************************************************
void print_help(void){
  int a;
  char chunk_size_str[10];
  char info_block_size_str[30];
  char platform_name_str[sizeof(platform_name) + 10] = "UNDEFINED";
  snprintf(chunk_size_str, sizeof(chunk_size_str) - 1, "%u", chunk_size);
  snprintf(info_block_size_str, sizeof(info_block_size_str) - 1, "Yes (align size := %u)", align_size);
  if(platform_name[0]) snprintf(platform_name_str, sizeof(platform_name_str) - 1, "%s", platform_name);
  char *usage[] =
    { "-k", "Path to kernel file", kernel_file,
      "-r", "Path to result file", res_file,
      "-e", "Convert byte order to big-endian", to_big_endian ? "Yes" : "No",
      "-c", "Use ECC", use_ecc ? "Yes" : "No",
      "-s", "FLASH Unit(Chunk) size", chunk_size_str,
      "-i", "Add image info block", add_image_info_block ? info_block_size_str : "No",
      "-p", "Platform name", platform_name_str,
      "-v", "Verbose output", verbose ? "Yes" : "No",
      "-h", "Show help and exit", "" };
  printf("Version := %s\nUsage:\n", PROGRAM_VERSION);
  for(a = 0; a < sizeof(usage) / sizeof(usage[0]); a += 3){
    printf("  %-5s%-40s%s\n", usage[a], usage[a + 1], usage[a + 2]);
  }
}//-----------------------------------------------------------------------------------

//************************************************************************************
/* выполняет конвертирование(если нужно) порядка байт для полей структуры packed_tags2 */
void convert_endian_v2(struct yaffs_packed_tags2 *pt, int for_tags_ecc){
  if(for_tags_ecc){
    swap(pt->ecc.col_parity);
    swap(pt->ecc.line_parity);
    swap(pt->ecc.line_parity_prime);
  }else{
    swap(pt->t.chunk_id);
    swap(pt->t.n_bytes);
    swap(pt->t.obj_id);
    swap(pt->t.seq_number);
  }
}//-----------------------------------------------------------------------------------

//************************************************************************************
/* выполняет конвертирование(если нужно) порядка байт для полей структуры packed_tags1
   смотри yaffs_packedtags1.c для понимания какие поля используются */
void convert_endian_v1(struct yaffs_packed_tags1 *pt){
  struct yaffs_packed_tags1 res;
  memset(&res, 0xFF, sizeof(res));
  set_be_bfv(res, chunk_id, pt->chunk_id);
  set_be_bfv(res, serial_number, pt->serial_number);
  set_be_bfv(res, n_bytes, pt->n_bytes);
  set_be_bfv(res, obj_id, pt->obj_id);
  set_be_bfv(res, ecc, pt->ecc);
  set_be_bfv(res, deleted, pt->deleted);
  memcpy((void *)pt, &res, sizeof(res));
}//-----------------------------------------------------------------------------------

//************************************************************************************
/* считает ECC для блока данных. oob_offset это смещение в buf с которого
   начинается oob. оно идет сразу после области данных.
   !!! не делай тут assert !!! просто возвращай -1 !!! */
int calc_ecc_for_data(unsigned char *data, int data_len, unsigned char *res_buf, int res_buf_size){
  unsigned char *res_ecc = res_buf;
  unsigned char ecc_buf[3]; //3 байта на каждый блок
  int a;
  //если data не передан то нас просто спрашивают про размер res_buf
  if(res_buf == NULL || data == NULL)
    return data_len / ECC_BLOCK_SIZE * sizeof(ecc_buf);
  //защита от дурака. мы дальше в коде полагаемся на то, что эти значения именно такие!
  if(!(res_buf_size == ecc_layout.eccbytes)) return -1;
  //оно должно быть одинаково (смотри ниже описание !) иначе вылетим хер знает куда в памяти !
  if(!(data_len / ECC_BLOCK_SIZE * 3 == res_buf_size)) return -1;
  for(a = 0; a < data_len / ECC_BLOCK_SIZE; a++){
    //считаем блоками по 256 байт! на выходе три байта ecc
    yaffs_ecc_calc(data + a * ECC_BLOCK_SIZE, ecc_buf);
    //нужно переставить первые два байта для систем с порядком байт отличным от нашей
    if(endian_need_conv)
      exchg(ecc_buf[0], ecc_buf[1]);
    //запишем высчитанный для блока ecc(3 байта)
    if(!(res_buf + res_buf_size >= res_ecc + sizeof(ecc_buf))) return -1;
    memcpy(res_ecc, ecc_buf, sizeof(ecc_buf));
    res_ecc += sizeof(ecc_buf);
  }
  return 0;
}//-----------------------------------------------------------------------------------

//************************************************************************************
/* выполняет копирование из ecc_buf(рассчитанный для блока data ecc)
   и free_buf(yaffs[1,2]tags) в указанные в oob_layout-е позиции
   !!! не делай тут assert !!! просто возвращай -1 !!!
   ecc_buf - буфер с данными о ecc data
   free_buf - буфер с данными о tags
     (packed tags1 или packed tags2 или packed tags2 + tags2 ecc)
   названия такие выбраны в созвучие с именами полей ecc_layout-а.
*/
int copy_data_to_oob(unsigned char *oob, int oob_size,
                      unsigned char *ecc_buf, int ecc_len,
                      unsigned char *free_buf, int free_len){
  int a, x, l, len;
  uint32_t pos;
  unsigned char *src, *dst;
  //копируем ecc_buf в oob[..eccpos..]
  for(a = 0; a < ecc_layout.eccbytes; a++){
    pos = ecc_layout.eccpos[a];
    src = &ecc_buf[a];
    if(!(src < ecc_buf + ecc_len)) return -1; //проверка src на вылет за границы дозволенного
    dst = &oob[pos];
    if(!(dst < oob + oob_size)) return -2; //проверка dst на вылет за границы дозволенного
    //ok
    *dst = *src;
  }
  //копируем free_buf в oob[..oobfree..]
  a = 0;
  for(x = 0; x < MTD_MAX_OOBFREE_ENTRIES; x++){
    len = ecc_layout.oobfree[x].length;
    if(len <= 0) break; //чуть что сразу выходим
    for(l = 0; l < len && a < free_len; l++, a++){
      pos = ecc_layout.oobfree[x].offset + l;
      src = &free_buf[a];
      if(!(src < free_buf + free_len)) return -3; //проверка src на вылет за границы дозволенного
      dst = &oob[pos];
      if(!(dst < oob + oob_size)) return -4; //проверка dst на вылет за границы дозволенного
      //ok
      *dst = *src;
    }
  }
  return 0;
}//-----------------------------------------------------------------------------------

//************************************************************************************
/* выполняет обсчет data ecc и запись free_buf + data_ecc в oob согласно ecc_layout-у
   эта независящая от номера yaffs функция(1 или 2) так как free_buf передается нам уже
   посчитанным(вон он как раз и зависит от номера yaffs-а) а обсчет data ecc одинаков
   для обоих yaffs-ов.
*/
void calc_ecc_for_data_and_copy_all_to_oob
(unsigned char *buf, int buf_size, int oob_offset, void *free_buf, int free_len){
  int ret;
  unsigned char *ecc_res_buf = NULL;
  int ecc_res_buf_len = 0;
  assert(buf_size > chunk_data_size);
  //получим необходимый размер для ecc_res_buf
  ecc_res_buf_len = calc_ecc_for_data(buf, chunk_data_size, NULL, 0);
  assert(ecc_res_buf_len > 0);
  //!!! до free никаких assert-ов !!!
  ecc_res_buf = malloc(ecc_res_buf_len);
  if(!ecc_res_buf){
    fprintf(stderr, "Can't alloc memory for ecc_result\n");
    exit(-1);
  }
  if(calc_ecc_for_data(buf, chunk_data_size, ecc_res_buf, ecc_res_buf_len) < 0){
    fprintf(stderr, "Error calc ecc for data!\n");
    free(ecc_res_buf);
    exit(-1);
  }
  /* итак если мы здесь то успешно удалось посчитать ecc для tags и для data
     и мы уверены, что ecc_res_buf_len == ecc_layout.eccbytes
     (это и соразмерность data проверила ф-я calc_ecc_for_data).
     теперь запишем то что нассчитали в oob согласно ecc_layout-у */
  ret = copy_data_to_oob(buf + oob_offset, buf_size - oob_offset,
                         ecc_res_buf, ecc_res_buf_len, free_buf, free_len);
  if(ret < 0){
    fprintf(stderr, "Error copy data to oob. ret = %d!\n", ret);
    free(ecc_res_buf);
    exit(-1);
  }
  //All ok
  free(ecc_res_buf);
}//-----------------------------------------------------------------------------------

//************************************************************************************
//выполняет заполнение tags2(spare data из oob - информация используемая yaffs для идентификации чанков)
//oob_offset это смещение в buf с которого начинается oob. оно идет сразу после области данных.
void cook_tags_v2(unsigned char *buf, int buf_size, int oob_offset, unsigned int obj_id,
                  unsigned int seq_number, unsigned int chunk_id, unsigned int n_bytes, int extra_tags){
  struct yaffs_packed_tags2 pt; //packed tags2(tags2 + ecc)
  struct yaffs_ext_tags t; //extra tags. используются для паковки в yaffs_packed_tags2
  //защита от дурака
  if(use_ecc){
    assert(chunk_oob_total_size <= buf_size - oob_offset); //oob size рассчитан верно и влазит в хвост buf
    //в хвост buf влазит pt(tags2 + tags ecc) + data ecc
    assert(sizeof(pt) + ecc_layout.eccbytes <= buf_size - oob_offset);
  }else{
    assert(sizeof(pt.t) <= buf_size - oob_offset); //tags2_only_tags влазит в хвост buf(место для tags)
  }
  /* для примера смотри utils/mkyaffs2image.c->write_chunk(). она делает нечно похожее. */
  //начинаем подготовку сткрутуры yaffs_ext_tags для последующей ее упаковки в структуру yaffs_packed_tags2
  memset(&pt, 0xFF, sizeof(pt));
  memset(&t, 0xFF, sizeof(t));
  //заполняем структуру yaffs_ext_tags
  t.chunk_id = chunk_id;
  t.n_bytes = n_bytes;
  t.obj_id = obj_id;
  t.seq_number = seq_number;
  //если нужно добавить extra_tags(используется только для obj header чанков)
  if(extra_tags){
     /* это делается путем запаковки extra tags в поля obj_id и chunk_id. смотри ф-ю yaffs_pack_tags2_tags_only !
        мы полагаем что: extra_is_shrink == 0, extra_shadow == 0, extra_obj_type == файл, extra_file_size == 0
        (так было во всех сторонних дампах что я анализировал) */
    t.extra_available = 1; //флаг того что присутствуют extra tags
    t.extra_parent_id = YAFFS_OBJECTID_ROOT; //id родителя. наш файл лежит в корне.
    t.extra_obj_type = YAFFS_OBJECT_TYPE_FILE; //тип объекта - файл
    t.extra_file_size = 0; //так было во всех дампах что я анализировал
  }
  /* тут код аналогичен yaffs_pack_tags2 но для правильного обсчета tags ecc с
     учетом разных endian пришлось сделать вот так
  */
  //пакуем. сначала tags. без ecc ! даже если use_ecc == 1 !
  yaffs_pack_tags2_tags_only(&(pt.t), &t);
  //сконвертируем порядок байт для tags. это крайне важно сделать ДО обсчета tags ecc!
  convert_endian_v2(&pt, 0);
  //если мы используем ecc
  if(use_ecc){
    //считаем ecc для tags
    yaffs_ecc_calc_other((unsigned char *)&(pt.t), sizeof(struct yaffs_packed_tags2_tags_only), &(pt.ecc));
    //сконвертируем порядок байт для tags ecc
    convert_endian_v2(&pt, 1);
    //считаем ecc для данных
    calc_ecc_for_data_and_copy_all_to_oob(buf, buf_size, oob_offset, (void*)&pt, sizeof(pt));
  }else{
    //oob тут не используется(NOR) => просто запишем pt.t линейно сразу после data
    memcpy(buf + oob_offset, &pt.t, sizeof(pt.t));
  }
}//-----------------------------------------------------------------------------------

//************************************************************************************
//выполняет заполнение tags1
void cook_tags_v1(unsigned char *buf, int buf_size, int oob_offset, unsigned int obj_id,
                  unsigned int seq_number, unsigned int chunk_id, unsigned int n_bytes, int extra_tags){
  struct yaffs_packed_tags1 pt;
  struct yaffs_tags *tags1 = (void*)&pt;
  struct yaffs_ext_tags t; //extra tags. используются для паковки в yaffs_packed_tags2
  //защита от дурака
  if(use_ecc){
    assert(chunk_oob_total_size <= buf_size - oob_offset); //oob size рассчитан верно и влазит в хвост buf
    assert(sizeof(*tags1) + ecc_layout.eccbytes <= buf_size - oob_offset); //в хвост buf влазит tags1 + data ecc
  }else {
    assert(use_ecc == 1); //! не реализовано для yaffs1 и не ecc флешек !
  }
  /* для примера смотри utils/mkyaffsimage.c->write_chunk(). она делает нечно похожее. */
  //начинаем подготовку сткрутуры yaffs_ext_tags для последующей ее упаковки в структуру yaffs_packed_tags1
  memset(&pt, 0xFF, sizeof(pt));
  memset(&t, 0xFF, sizeof(t));
  //заполняем структуру yaffs_ext_tags для yaffs1
  t.chunk_id = chunk_id;
  t.serial_number = 1; //всегда 1 ?
  t.n_bytes = n_bytes;
  t.obj_id = obj_id;
  t.is_deleted = 0;
  //пакуем
  yaffs_pack_tags1(&pt, &t);
  //сконвертируем порядок байт для tags. это крайне важно сделать ДО обсчета tags ecc!
  convert_endian_v1(&pt);
  //если мы используем ecc
  if(use_ecc){
    //считаем ecc для tags
    yaffs_calc_tags_ecc(tags1);
    //считаем ecc для данных
    calc_ecc_for_data_and_copy_all_to_oob(buf, buf_size, oob_offset, (void*)tags1, sizeof(*tags1));
  }//else не реализовано !
}//-----------------------------------------------------------------------------------

/* вызов функции подготовки tags в зависимости от используемой версии yaffs */
#define cook_tags(args...){ \
  if(yaffs_version == 2)    \
    cook_tags_v2(args);   \
  if(yaffs_version == 1)  \
    cook_tags_v1(args);   \
}

//************************************************************************************
/* заполняет заголовок для объекта
  обязательно забей память 0xff перед вызовом этой функции! */
void cook_object_header(unsigned char *buf, char *name){
  struct yaffs_obj_hdr *oh = (struct yaffs_obj_hdr *)buf;
  struct stat *s = &kernel_file_stats;
  sswp(oh->type, YAFFS_OBJECT_TYPE_FILE);
  sswp(oh->parent_obj_id, YAFFS_OBJECTID_ROOT);
  memset(oh->name, 0, sizeof(oh->name));
  strncpy(oh->name, name, YAFFS_MAX_NAME_LENGTH);
  sswp(oh->yst_mode, 0100644);
  sswp(oh->yst_uid, 0);
  sswp(oh->yst_gid, 0);
  sswp(oh->yst_atime, 0);
  sswp(oh->yst_mtime, 0);
  sswp(oh->yst_ctime, 0);
  sswp(oh->yst_rdev, s->st_rdev);
  sswp(oh->file_size_low , s->st_size);
  sswp(oh->file_size_high, s->st_size >> 32);
}//-----------------------------------------------------------------------------------

//************************************************************************************
/* осуществляет заполнение заголовка для объекта(0-й чанки) и запись чанки в файл результата(r) */
int fill_and_write_obj_header(int r, int obj_id, int seq_number, unsigned char *buf, int buf_size, int *n){
  int len;
  int chunk_id = 0; //для obj header номер чанки == 0
  int n_bytes = 0; //для чанки содержащей заголовок объекта n_bytes всегда == 0
  //подготовим буфер к заполнению
  memset(buf, 0xff, buf_size);
  verb_printf("%u: Writing chunk = 0 for obj(%u) HEADER, seq = %u\n", (*n)++, obj_id, seq_number);
  //заполним заголовок объекта
  cook_object_header(buf, KERNEl_YAFFS_FILE_NAME);
  //заполним данные tags + ecc(oob part)
  cook_tags(buf, buf_size, chunk_data_size, obj_id, seq_number, chunk_id, n_bytes, 1);
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
  unsigned int buf_size = chunk_full_size; //для наглядности. размер буфера. он равен полному размеру чанки(data + oob) !
  unsigned int hole_fill_buf_size = chunk_full_size; //аналогично buf_size. они всегда одинаковые !
  unsigned char *buf = NULL; //буфер в котором мы будем собирать чанку. он состоит из данных + tags(oob, metadata)
  char *hole_fill_buf = NULL; //буфер для заполнения дыр
  unsigned int hole_size; //для вымеривания дырок в конце блока
  int rlen, wrlen; //для read и write операций
  int block_wrbc = 0; //для подсчета сколько байт блока было записано
  int n = 0; //для подсчета сколько чанков было всего записано(заполнители дыр сюда не входят!)
  int bc = 0; //для подсчета сколько блоков было записано
  int total_wrbc = 0; //для подсчета сколько всего было записано байт
  define_ib_vars(); //объявим переменные необходимые для работы с info блоком
  //выделим память для буферов
  if((buf = malloc(buf_size)))
    hole_fill_buf = malloc(hole_fill_buf_size);
  if(!buf || !hole_fill_buf){
    perror("Can't malloc memory for buffers");
    goto end;
  }
  //заполним буфер для дыр
  memset(hole_fill_buf, 0xff, hole_fill_buf_size);
  //инит инфо блока и резервирование под него места в начале файла образа
  if(add_image_info_block){
    memset(info_block_buf, 0x0, info_block_size);
    if(write(r, info_block_buf, info_block_size) != info_block_size){
      perror("Can't write info_block place holder to result file! error descr");
      goto end;
    }
  }
  //заполним заголовочную чанку и запишем ее в r
  wrlen = fill_and_write_obj_header(r, obj_id, seq_number, buf, buf_size, &n);
  total_wrbc += wrlen; //считаем сколько всего байт мы записали
  block_wrbc += wrlen; //считаем сколько байт блока мы уже записали

  //подготовим буфер к заполнению
  memset(buf, 0xff, buf_size);
  chunk_id++; //переходим к следующей чанке. это уже будет чанка с данными.
  //читаем из файла ядра чанками размером $chunk_data_size
  while((rlen = read(k, buf, chunk_data_size)) > 0){
    //если места в блоке уже не осталось чтобы вместить текущую чанку
    if(block_wrbc + buf_size > block_size){
      hole_size = block_size - block_wrbc; //рассчитаем размер дыры в хвосте блока
      if(hole_size > 0){
        verb_printf("Writing hole filler: seq = %u, offset = %u, hole_size = %u\n",  seq_number, block_wrbc, hole_size);
        if((wrlen = write(r, hole_fill_buf, hole_size)) != hole_size){
          fprintf(stderr, "Can't write hole fill data %u: %u to result file! ", obj_id, seq_number);
          perror("error descr");
          goto end;
        }
      }else{
        wrlen = 0; //мы же ничего не записали! нужно обнулить иначе total_wrbc неверно посчитается!
        verb_printf("Perfect block - no hole in the tail\n");
      }
      //начнем использование нового блока
      seq_number++;
      block_wrbc = 0;
      bc++;
      total_wrbc += wrlen;
    }
    //пищем текущую чанку в файл r
    verb_printf("%u: Writing chunk = %u for obj(%u), seq = %u, data len = %u, chunk_len = %u\n", n++, chunk_id, obj_id, seq_number, rlen, buf_size);
    //рассчитаем yaffs2 tags для текущей чанки
    cook_tags(buf, buf_size, chunk_data_size, obj_id, seq_number, chunk_id++, rlen, 0);
    if((wrlen = write(r, buf, buf_size)) != buf_size){
      fprintf(stderr, "Can't write data chunk %u: %u->%u to result file! ", obj_id, seq_number, chunk_id);
      perror("error descr");
      goto end;
    }
    total_wrbc += wrlen; //считаем сколько всего было записано байт
    block_wrbc += wrlen; //считаем сколько байт блока мы уже записали
    //подготовим буфер к заполнению(уже для следующей чанки)
    memset(buf, 0xff, buf_size);
  }

  /* данные записали. осталось заполнить оставшееся до конца блока место.
     считать ecc для этих данных не нужно так как оно и так для массива 0xff будет равно 0xff */
  hole_size = block_size - block_wrbc; //рассчитаем размер огромной дыры в хвосте блока
  verb_printf("All data has been writen. But to the end of the block we left %u bytes. Lets fill it.\n", hole_size);
  while(hole_size > 0){
    rlen = hole_size > hole_fill_buf_size ? hole_fill_buf_size : hole_size; //сколько нужно записать данные в этом проходе цикла
    if(rlen == hole_fill_buf_size) verb_printf("%u: ", n++); else verb_printf("LAST: ");
    verb_printf("Writing hole fill block: seq = %u, offset = %u, left_hole_size = %u, len = %u\n", seq_number, block_wrbc, hole_size, rlen);
    if((wrlen = write(r, hole_fill_buf, rlen)) != rlen){
      fprintf(stderr, "Can't write hole fill data %u: %u to result file! ", obj_id, seq_number);
      perror("error descr");
      goto end;
    }
    total_wrbc += wrlen;
    block_wrbc += wrlen;
    hole_size -= wrlen;
  }
  bc++;
  verb_printf("\n");
  //последняя проверка. вдруг гдето ошиблись.
  if(total_wrbc % block_size != 0 && total_wrbc / block_size != bc){
    printf("Warning! Something went wrong!\n");
  }
  //заполним инфо блок
  if(add_image_info_block){
    //magic. чтобы отличать наш образ от других!
    strncpy(ib_var_tmp_buf, "MIKROTIK", sizeof(ib_var_tmp_buf));
    add_ib_var(0, 1); //слово MIKROTIK как флаг обозначающий что это инфоблок порожденный этой программой
    //имя платформы: NOR, NAND, etc...
    strncpy(ib_var_tmp_buf, platform_name, sizeof(ib_var_tmp_buf));
    add_ib_var(0, 1);
    add_ib_var(info_block_size); //размер info блока
    add_ib_var(total_wrbc); //полный размер образа(без учета info блока)
    add_ib_var(block_size); //размер блока yaffs2. он же кратен размеру образа
    add_ib_var(total_wrbc / block_size); //размер образа в блоках
    add_ib_var(chunk_data_size); //размер области полезных данных в чанке
    add_ib_var(chunk_oob_total_size); //размиер области oob в чанке
    add_ib_var(chunk_full_size); //общий размер чанки(равен сумме двух предидущих полей)
    add_ib_var(chunks_per_block); //сколько чанок вмещает один блок
    add_ib_var(align_size); //размер align_size(обычно 65536 или 0)
  }
  //выведем статистику по проделанной работе.
  printf("Successfully writed %u blocks and %u bytes\n", bc, total_wrbc);
  printf("Each block contain %u chanks + %u bytes tail hole.\n", chunks_per_block, block_size - chunks_per_block * chunk_full_size);
  printf("Each chunk(%u bytes) consists: data part(%u bytes) + oob part(%u bytes).\n", chunk_full_size, chunk_data_size, chunk_oob_total_size);
end:
  //освобождаем память
  if(buf)
    free(buf);
  if(hole_fill_buf)
    free(hole_fill_buf);
}//-----------------------------------------------------------------------------------

//************************************************************************************
/* рассчитывает нужные для работы переменные
   (block_size, chunk_data_size, chunk_full_size, etc...) */
int calc_needed_vars(void){
  int raw_block_size = 0;
  /* размер области полезных данных чанки но без oob */
  chunk_data_size = chunk_size;
  //для такой маленькой чанки перехордим на использование Yaffs1
  if(chunk_size < 1024)
    yaffs_version = 1;
  //кол-во чанок в блоке
  switch(chunk_size){
    case 512:
      chunks_per_block = 32;
      break;
    default:
      chunks_per_block = 64;
  }
  /* размер блока(в терминах yaffs2. A Group of chunks(a block is the unit of erasure). Все страницы(чанки) блока имеют одинаковый sequence number!
     все это нужно именно из за того что мы можем стирать данные только одним блоком! так что нужно знать какие чанки принадлежат этому блоку. */
  block_size = chunk_data_size * chunks_per_block;
  raw_block_size = block_size; //нужно для вывода статистики в printf
  /* размер всей чанки (данные чанки + oobfree->offset + tags + if(use_ecc){ tags ecc + data ecc }). */
  chunk_full_size = chunk_data_size;
  if(use_ecc){ //если используется ECC
    chunk_oob_free_size = get_oobfree_len(ecc_layout.oobfree); //получим суммарную длину oobfree
    //защита от дурака(эта структура обязана влазить в oobfree.length !)
    if(yaffs_version == 1)
      assert(sizeof(struct yaffs_tags) <= chunk_oob_free_size);
    if(yaffs_version == 2)
      assert(sizeof(struct yaffs_packed_tags2) <= chunk_oob_free_size);
    //размер всей oob области чанки
    chunk_oob_total_size = get_ecc_layout_max_offset(&ecc_layout) + 1; //так как offset там начиная с 0
    assert(chunk_oob_free_size + ecc_layout.eccbytes <= chunk_oob_total_size);
    //учитываем в полном размере чанки еще и размер oob!
    chunk_full_size += chunk_oob_total_size;
    //учитываем в размере блока еще и размер oob!
    block_size += chunk_oob_total_size * chunks_per_block;
  }else{ //ECC не используется. это скорей всего NOR флешка. у нее размер oob == 16 байт(размер tags2 структуры но без tags ecc)
    //размер всей oob области == размеру структуры tags2_tags_only! БеЗ { tags ecc и data ecc } !
    chunk_oob_total_size = sizeof(struct yaffs_packed_tags2_tags_only);
    chunk_full_size += chunk_oob_total_size;
  }
  //кол-во чанок в блоке. уточнение верхнего значения с учетом вновь выссчитанных данных(это важно для NOR-а!)
  chunks_per_block = block_size / chunk_full_size;
  /* размер info блока. ! его размер + переданный нам align_size должны быть кратны block_size !
     align_size нужен для openwrt-шного sysupgrade скрипта(он добавит к нашему образу еще свой
     заголовок размером align_size) чтобы указать dd смещение в блоках от начала sysupgrade.bin
       [...sysupgrade header(size = align_size = 65536)...][...info block(size = block_size - info_block_size)...]
       [< < < < < < < <   sizeof(sysupgrade header) + sizeof(info_block header) == block_size   > > > > > > > > >]
  */
  if(add_image_info_block){
    info_block_size = (align_size + 1024) / block_size;
    info_block_size += 1;
    info_block_size *= block_size;
    info_block_size -= align_size;
  }else{
    info_block_size = 0;
  }
  //последние проверки
  assert(chunk_oob_free_size <= chunk_oob_total_size);
  //выведем то что мы посчитали
  verb_printf("YAFFS%d parameters vars:\n", yaffs_version);
  verb_printf("  chunk_data_size := %u\n", chunk_data_size);
  verb_printf("  chunk_full_size := %u\n", chunk_full_size);
  verb_printf("  chunk_oob_total_size := %u\n", chunk_oob_total_size);
  verb_printf("  chunk_oob_free_size := %u\n", chunk_oob_free_size);
  verb_printf("  ecc_layout.eccbytes := %u\n", ecc_layout.eccbytes);
  //если ДЫРЫ в ecc layout-е
  if(chunk_oob_free_size + ecc_layout.eccbytes != chunk_oob_total_size)
    verb_printf("  ecc_layout HOLES := %u\n", chunk_oob_total_size - (chunk_oob_free_size + ecc_layout.eccbytes));
  verb_printf("  block_size := %u(%u + %u)\n", block_size, raw_block_size, block_size - raw_block_size);
  verb_printf("  chunks_per_block := %u\n", chunks_per_block);
  verb_printf("\n");
  return 0;
}//-----------------------------------------------------------------------------------

//************************************************************************************
int main(int argc, char *argv[]){
  int k = 0;
  int r = 0;
  int ch; //для парсинга параметров
  //загружаем параметры командной строки
  while( (ch = getopt(argc, argv, "k:r:s:i:p:cevh")) != -1){
    switch(ch){
      case 'k': snprintf(kernel_file, sizeof(kernel_file) - 1, "%s", optarg); break;
      case 'r': snprintf(res_file, sizeof(res_file) - 1, "%s", optarg); break;
      case 'c': use_ecc = 1; break;
      case 'e': to_big_endian = 1; break;
      case 's': chunk_size = atoi(optarg); break;
      case 'i': add_image_info_block = 1; align_size = atoi(optarg); break;
      case 'p': strncpy(platform_name, optarg, sizeof(platform_name)); break;
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
  /* получим ecc layout для указанного chunk_size.
     если флаг use_ecc не установлен то вернет структуру - заглушку. */
  ecc_layout = get_ecclayout_by_chunk_size(use_ecc ? chunk_size : 0);
  //рассчитаем нужные для работы переменные(block_size, chunk_data_size, chunk_full_size, etc...)
  if(calc_needed_vars()) goto end;
  k = open(kernel_file, O_RDONLY);
  if(k <= 0){
    perror("Can't open kernel file");
    exit(-1);
  }
  r = open(res_file, O_CREAT | O_TRUNC | O_WRONLY, 0644);
  if(r <= 0){
    perror("Can't create result file");
    close(k);
    exit(-1);
  }
  //выделим память для info_block-а
  if(add_image_info_block && info_block_size > 0){
    info_block_buf = malloc(info_block_size);
    if(!info_block_buf){
      perror("Can't malloc memory for info_block_buf");
      goto end;
    }
  }
  do_pack(k, r);
  //запишем инфо блок
  if(add_image_info_block){
    //переходим к началу info блока(он уже заранее был проинициализирован pначениями 0x0)
    if(lseek(r, 0, SEEK_SET) == 0){
      if(write(r, info_block_buf, info_block_size) != info_block_size)
        perror("Can't write info_block place holder to result file! error descr");
    }else{
      perror("Can't seek to begin of result file");
    }
    printf("Info block write done(0..%u) from begin\n", info_block_size - 1);
  }
end:
  if(info_block_buf)
    free(info_block_buf);
  close(k);
  close(r);
  return 0;
}//-----------------------------------------------------------------------------------
