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
#include "yaffs2/yaffs_packedtags2.h"

#include "kernel2minor.h"
#include "nand_ecclayout.h"

//размер блока с инфо данными о созданном образе
#define INFO_BLOCK_SIZE 256
//имя файла ядра в файловой система yaffs2
#define KERNEl_YAFFS_FILE_NAME "kernel"
/* сколько чанок в одном блоке. обычно 64! для NOR меньше!
   используй только для рассчетов в calc_needed_vars!
   реальное значение кол-ва чанок в блоке содержится в
   переменной chunks_per_block ! */
#define CHUNKS_PER_BLOCK 64
/* размер блока данных по которому считается ECC. для yaffs2 это 256 */
#define ECC_BLOCK_SIZE 256

char info_block_buf[INFO_BLOCK_SIZE];
char kernel_file[255];
char res_file[255];
struct stat kernel_file_stats;
nand_ecclayout_t ecc_layout;

//значения по умолчанию для наших параметров
int endians_need_conv = 0; //нужна ли конвертация порядка байт
int chunk_size = 1024; //по умолчанию == размеру для NOR флешки
int use_ecc = 0; //используется ли ECC
int verbose = 0; //говорливость в stdout
//добавить к образу блок с данными описывающими его параметры(размер, blocksize, chunksize, etc...)
int add_image_info_block = 0; //это нужно для образов используемых перепрошивальщиком openwrt(для nand флешей)

//параметры для создаваемой нами файловой системы yaffs2. рассчитываются ф-ей calc_needed_vars.
static int chunk_data_size = 0;
static int chunk_full_size = 0;
static int chunk_oob_size = 0;
static int chunk_tags_offset = 0;
static int block_size = 0;
static int chunks_per_block = 0;

//************************************************************************************
void print_help(void){
  int a;
  char chunk_size_str[10];
  snprintf(chunk_size_str, sizeof(chunk_size_str) - 1, "%u", chunk_size);
  char *usage[] =
    { "-k", "Path to kernel file", kernel_file,
      "-r", "Path to result file", res_file,
      "-e", "Enable endians convert", endians_need_conv ? "Yes" : "No",
      "-c", "Use ECC", use_ecc ? "Yes" : "No",
      "-s", "FLASH Unit(Chunk) size", chunk_size_str,
      "-i", "Add image info block", add_image_info_block ? "Yes" : "No",
      "-v", "Verbose output", verbose ? "Yes" : "No",
      "-h", "Show help and exit", "" };
  printf("Usage:\n");
  for(a = 0; a < sizeof(usage) / sizeof(usage[0]); a += 3){
    printf("  %-5s%-25s%s\n", usage[a], usage[a + 1], usage[a + 2]);
  }
}//-----------------------------------------------------------------------------------

//************************************************************************************
/* выполняет конвертирование(если нужно) порядка байт для полей структуры pt */
void convert_endians(struct yaffs_packed_tags2 *pt, int for_tags_ecc){
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
/* считает ECC для блока данных. oob_offset это смещение в buf с которого
   начинается oob. оно идет сразу после области данных. */
void calc_ecc_for_data(char *buf, unsigned int buf_size, unsigned int oob_offset){
  int data_len = chunk_data_size;
  unsigned char *data = (void *)buf;
  unsigned char *res_ecc;
  unsigned char ecc_buf[3]; //3 байта на каждый блок
  int a, e;
  //защита от дурака. мы дальше в коде полагаемся на то, что эти значения именно такие!
  assert(buf_size - oob_offset == chunk_oob_size);
  assert(buf_size - chunk_oob_size == chunk_data_size);
  //оно должно быть одинаково (смотри ниже описание !) иначе вылетим хер знает куда в памяти !
  assert(data_len / ECC_BLOCK_SIZE == ecc_layout.eccbytes / 3);
  for(a = 0; a < data_len / ECC_BLOCK_SIZE; a++){
    //считаем блоками по 256 байт! на выходе три байта ecc
    yaffs_ecc_calc(data + a * ECC_BLOCK_SIZE, ecc_buf);
    //нужно переставить первые два байта для систем с порядком байт отличным от нашей
    if(endians_need_conv)
      exchg(ecc_buf[0], ecc_buf[1]);
    /* eccpos это массив ecc по 3 штуки(байта) на каждые 256 байт флешки(одна страница 2048 байт дробится
       на 8 штук по 256 байт и по ним считается ecc!). запишем выссчитанные байты ecc в нужные позиции.
       какие именно позиции - указано в eccpos. */
    for(e = 0; e < 3; e++){
      res_ecc = data + oob_offset + ecc_layout.eccpos[a * 3 + e];
      //защита от бага вылета за пределы buf
      assert((void *)buf + buf_size > (void *)res_ecc);
      //записываем высчитанный ранее результат
      *res_ecc = ecc_buf[e];
    }
  }
}//-----------------------------------------------------------------------------------

//************************************************************************************
//выполняет заполнение tags(spare data из oob - информация используемая yaffs для идентификации чанков)
//oob_offset это смещение в buf с которого начинается oob. оно идет сразу после области данных.
void cook_tags(char *buf, unsigned int buf_size, unsigned int oob_offset, unsigned int obj_id,
               unsigned int seq_number, unsigned int chunk_id, unsigned int n_bytes, int extra_tags){
  unsigned int tags_offset = oob_offset + chunk_tags_offset; //смещение от начала buf с которого начинается область tags
  struct yaffs_packed_tags2 *pt = (void*)(buf + tags_offset); //packed tags2(tags2 + ecc)
  struct yaffs_ext_tags t; //extra tags. используются для паковки в yaffs_packed_tags2
  //защита от дурака
  if(use_ecc){
    assert(sizeof(pt) <= buf_size - tags_offset); //pt влазит в хвост buf(место для tags + tags ecc)
    assert(chunk_oob_size <= buf_size - oob_offset); //размер области oob влазит в хвост buf(место для oob)
  }else {
    assert(sizeof(pt->t) <= buf_size - tags_offset); //tags2_only_tags влазит в хвост buf(место для tags)
  }
  /* для примера смотри utils/mkyaffs2image.c->write_chunk(). она делает нечно похожее. */
  //начинаем подготовку сткрутуры yaffs_ext_tags для последующей ее упаковки в структуру yaffs_packed_tags2
  memset(&t, 0x0, sizeof(t));
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
     учетом разных endians пришлось сделать вот так
  */
  //пакуем. сначала tags. без ecc ! даже если use_ecc == 1 !
  yaffs_pack_tags2_tags_only(&pt->t, &t);
  //сконвертируем порядок байт для tags. это крайне важно сделать ДО обсчета tags ecc!
  convert_endians(pt, 0);
  //если мы используем ecc
  if(use_ecc){
    //считаем ecc для tags
    yaffs_ecc_calc_other((unsigned char *)&pt->t, sizeof(struct yaffs_packed_tags2_tags_only), &pt->ecc);
    //сконвертируем порядок байт для tags ecc
    convert_endians(pt, 1);
    //считаем ecc для данных
    calc_ecc_for_data(buf, buf_size, oob_offset);
  }
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
  char *buf = NULL; //буфер в котором мы будем собирать чанку. он состоит из данных + tags(oob, metadata)
  char *hole_fill_buf = NULL; //буфер для заполнения дыр
  unsigned int hole_size; //для вымеривания дырок в конце блока
  int rlen, wrlen; //для read и write операций
  int block_wrbc = 0; //для подсчета сколько байт блока было записано
  int n = 0; //для подсчета сколько чанков было всего записано(заполнители дыр сюда не входят!)
  int bc = 0; //для подсчета сколько блоков было записано
  int total_wrbc = 0; //для подсчета сколько всего было записано байт
  u_int64_t *ib_ptr = (void*)info_block_buf;
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
    memset(info_block_buf, 0x0, sizeof(info_block_buf));
    if(write(r, info_block_buf, sizeof(info_block_buf)) != sizeof(info_block_buf)){
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
    add_ib_var(total_wrbc); //полный размер образа(без учета info блока)
    add_ib_var(block_size); //размер блока
    add_ib_var(chunk_data_size); //размер области полезных данных в чанке
    add_ib_var(chunk_oob_size); //размиер области oob в чанке
    add_ib_var(chunk_full_size); //общий размер чанки(равен сумме двух предидущих полей)
    add_ib_var(chunks_per_block); //сколько чанок вмещает один блок
  }
  //выведем статистику по проделанной работе.
  printf("Successfully writed %u blocks and %u bytes\n", bc, total_wrbc);
  printf("Each block contain %u chanks + %u bytes tail hole.\n", chunks_per_block, block_size - chunks_per_block * chunk_full_size);
  printf("Each chunk(%u bytes) consists: data part(%u bytes) + oob part(%u bytes).\n", chunk_full_size, chunk_data_size, chunk_oob_size);
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
void calc_needed_vars(void){
  int raw_block_size = 0;
  /* размер области полезных данных чанки но без oob */
  chunk_data_size = chunk_size;
  /* размер блока(в терминах yaffs2. A Group of chunks(a block is the unit of erasure). Все страницы(чанки) блока имеют одинаковый sequence number!
     все это нужно именно из за того что мы можем стирать данные только одним блоком! так что нужно знать какие чанки принадлежат этому блоку. */
  block_size = chunk_data_size * CHUNKS_PER_BLOCK;
  raw_block_size = block_size; //нужно для вывода статистики в printf
  /* смещение tags относительно конца блока данных */
  chunk_tags_offset = ecc_layout.oobfree[0].offset;
  /* размер всей чанки (данные чанки + oobfree->offset + tags + if(use_ecc){ tags ecc + data ecc }). */
  chunk_full_size = chunk_data_size;
  if(use_ecc){ //если используется ECC
    //защита от дурака(эта структура обязана влазить в oobfree.length !)
    assert(sizeof(struct yaffs_packed_tags2) <= ecc_layout.oobfree[0].length);
    //размер всей oob области чанки
    chunk_oob_size = ecc_layout.oobfree[0].offset + ecc_layout.oobfree[0].length + ecc_layout.eccbytes;
    //учитываем в полном размере чанки еще и размер oob!
    chunk_full_size += chunk_oob_size;
    //учитываем в размере блока еще и размер oob!
    block_size += chunk_oob_size * CHUNKS_PER_BLOCK;
  }else{ //ECC не используется. это скорей всего NOR флешка. у нее размер oob == 16 байт(размер tags2 структуры но без tags ecc)
    //размер всей oob области == размеру структуры tags2_tags_only! БеЗ { tags ecc и data ecc } !
    chunk_oob_size = sizeof(struct yaffs_packed_tags2_tags_only);
    chunk_full_size += chunk_oob_size;
  }
  //кол-во чанок в блоке
  chunks_per_block = block_size / chunk_full_size;
  //выведем то что мы посчитали
  verb_printf("YAFFS2 parameters vars:\n");
  verb_printf("  chunk_data_size := %u\n", chunk_data_size);
  verb_printf("  chunk_full_size := %u\n", chunk_full_size);
  verb_printf("  chunk_oob_size := %u\n", chunk_oob_size);
  verb_printf("  block_size := %u(%u + %u)\n", block_size, raw_block_size, block_size - raw_block_size);
  verb_printf("  chunks_per_block := %u\n", chunks_per_block);
  verb_printf("\n");
}//-----------------------------------------------------------------------------------

//************************************************************************************
int main(int argc, char *argv[]){
  int k;
  int r;
  int ch; //для парсинга параметров
  //загружаем параметры командной строки
  while( (ch = getopt(argc, argv, "k:r:s:icevh")) != -1){
    switch(ch){
      case 'k': snprintf(kernel_file, sizeof(kernel_file) - 1, "%s", optarg); break;
      case 'r': snprintf(res_file, sizeof(res_file) - 1, "%s", optarg); break;
      case 'c': use_ecc = 1; break;
      case 'e': endians_need_conv = 1; break;
      case 's': chunk_size = atoi(optarg); break;
      case 'i': add_image_info_block = 1; break;
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
  calc_needed_vars();

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
  //запишем инфо блок
  if(add_image_info_block){
    //переходим к началу info блока(он уже заранее был проинициализирован pначениями 0x0)
    if(lseek(r, 0, SEEK_SET) == 0){
      if(write(r, info_block_buf, sizeof(info_block_buf)) != sizeof(info_block_buf))
        perror("Can't write info_block place holder to result file! error descr");
    }else{
      perror("Can't seek to begin of result file");
    }
    printf("Info block write done(0..%zu bytes) from begin\n", sizeof(info_block_buf) - 1);
  }
  close(k);
  close(r);
  return 0;
}//-----------------------------------------------------------------------------------
