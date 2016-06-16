#ifndef __TEST_ECC_DATA_H__
#define __TEST_ECC_DATA_H__

/* Это набор тестовых функций для проверки data ecc. Они используются как в yaffs2 так и в yaffs1 так как
принцип подсчета ecc для data там один и тот же */

//дампит все байты блока ecc для данных
void dump_ecc_data(struct yaffs_packed_tags2 *pt){
  int a;
  //начало oob. Без смещения! так как в ecc pos указаны смещения от начала oob
  u8 *oob = ((u8*)pt) - noob.oobfree[0].offset;
  printf("ecc dd: ");
  for(a = 0; a < noob.eccbytes; a++){
    printf("%02x ", oob[noob.eccpos[a] & 0xFF]);
  }
  printf("\n");
}

//отладка алгоритма рассчета ecc для данных
//#define ECC_ALGO_DEBUG 1
#ifdef ECC_ALGO_DEBUG
#define ECC_ALGO_PRINTF printf
#else
#define ECC_ALGO_PRINTF(...)
#endif
//проверяет верность блока ecc для данных относительно самих данных
enum yaffs_ecc_result check_ecc_data(unsigned char *data, int data_len){
  unsigned char ecc_buf[9];
  unsigned char *read_ecc = &ecc_buf[0]; //ecc что у нас уже поссчитан и записан в oob
  unsigned char *test_ecc = &ecc_buf[3]; //ecc что мы посчитаем для сверки с read_ecc
  unsigned char *test_ecc_virgin = &ecc_buf[6]; //ecc что рассчитала ф-я yaffs_ecc_calc. test_ecc могла испортить ф-я yaffs_ecc_correct
  uint32_t ecc_rest_bytes = noob.eccbytes;
  int result;
  int a;
  //оно должно быть одинаково (смотри ниже описание !) иначе вылетим хер знает куда в памяти !
  assert(data_len / 256 == noob.eccbytes / 3);
  //все по аналогии с yaffs_unpack_tags2
  ECC_ALGO_PRINTF("ecc nn: ");
  for(a = 0; a < data_len / 256; a++){
    /* eccpos это массив ecc по 3 штуки(байта) на каждые 256 байт флешки(одна страница 2048 байт дробится
       на 8 штук по 256 байт и по ним считается ecc!). или отдна страница 512 байт дробится на две
       по 256 байт. рассчитываем начало этих трех байт ecc в блоке oob. */
    assert(ecc_rest_bytes >= 3); memcpy(read_ecc, data + data_len + noob.eccpos[a * 3], 3); ecc_rest_bytes -= 3;
//TODO: а тут нужно переставить байты ! иначе фигня получается! выясни что это за двух байтовое поле !!!!
    *((u16 *)read_ecc) = swap(*((u16 *)read_ecc));
    ECC_ALGO_PRINTF("%02x %02x %02x ", read_ecc[0], read_ecc[1], read_ecc[2]);
    //эта хрень считает блоками по 256 байт! и выплевывает три ecc.
    yaffs_ecc_calc(data + a * 256, test_ecc);
    memcpy(test_ecc_virgin, test_ecc, 3);
    result = yaffs_ecc_correct(data + a * 256, read_ecc, test_ecc);
    if(result != 0) {
      ECC_ALGO_PRINTF(" <<< err offset = %d ", noob.eccpos[a * 3]);
      ECC_ALGO_PRINTF(" >> vs needed: %02x %02x %02x ", test_ecc_virgin[0], test_ecc_virgin[1], test_ecc_virgin[2]);
      goto end;
    }
  }
end:
  ECC_ALGO_PRINTF("\n");
  switch (result) {
    case 0:
      return YAFFS_ECC_RESULT_NO_ERROR;
    case 1:
      return YAFFS_ECC_RESULT_FIXED;
    case -1:
      return YAFFS_ECC_RESULT_UNFIXED;
    default:
      return YAFFS_ECC_RESULT_UNKNOWN;
  }
}

char *ecc_result2str(int ecc_result){
  switch (ecc_result) {
    case YAFFS_ECC_RESULT_NO_ERROR:
      return "ECC_NO_ERROR";
    case YAFFS_ECC_RESULT_FIXED:
      return "ECC_FIXED";
    case YAFFS_ECC_RESULT_UNFIXED:
      return "ECC_UNFIXED";
    case YAFFS_ECC_RESULT_UNKNOWN:
      return "ECC_UNKNOWN";
    default:
      return "ECC_UNKNOWN!!!";
  }
}

#endif //__TEST_ECC_DATA_H__