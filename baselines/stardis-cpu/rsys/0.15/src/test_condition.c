/* Copyright (C) 2013-2023, 2025 Vincent Forest (vaplv@free.fr)
 *
 * The RSys library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The RSys library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the RSys library. If not, see <http://www.gnu.org/licenses/>. */

#if !defined(OS_WINDOWS)
  #define _POSIX_C_SOURCE 199506L
#endif

#include "condition.h"
#include "list.h"

#include <string.h>

#if defined(OS_WINDOWS)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <process.h>
#else
  #include <pthread.h>
#endif

static const char* src_str[] = {
"Rcvfbqr 1, XARR-QRRC VA GUR QRNQ:\n\
---------------------------------",

"BAPR LBH ORNG GUR OVT ONQNFFRF NAQ PYRNA BHG GUR ZBBA ONFR LBH'ER FHCCBFRQ GB\n\
JVA, NERA'G LBH? NERA'G LBH? JURER'F LBHE SNG ERJNEQ NAQ GVPXRG UBZR? JUNG\n\
GUR URYY VF GUVF? VG'F ABG FHCCBFRQ GB RAQ GUVF JNL!",

"VG FGVAXF YVXR EBGGRA ZRNG, OHG YBBXF YVXR GUR YBFG QRVZBF ONFR. YBBXF YVXR\n\
LBH'ER FGHPX BA GUR FUBERF BS URYY. GUR BAYL JNL BHG VF GUEBHTU.",

"GB PBAGVAHR GUR QBBZ RKCREVRAPR, CYNL GUR FUBERF BS URYY NAQ VGF NZNMVAT\n\
FRDHRY, VASREAB!",

"Rcvfbqr 2, GUR FUBERF BS URYY:\n\
------------------------------",

"LBH'IR QBAR VG! GUR UVQRBHF PLORE- QRZBA YBEQ GUNG EHYRQ GUR YBFG QRVZBF ZBBA\n\
ONFR UNF ORRA FYNVA NAQ LBH NER GEVHZCUNAG! OHG ... JURER NER LBH? LBH\n\
PYNZORE GB GUR RQTR BS GUR ZBBA NAQ YBBX QBJA GB FRR GUR NJSHY GEHGU.",

"QRVZBF SYBNGF NOBIR URYY VGFRYS!  LBH'IR ARIRE URNEQ BS NALBAR RFPNCVAT SEBZ\n\
URYY, OHG LBH'YY ZNXR GUR ONFGNEQF FBEEL GURL RIRE URNEQ BS LBH! DHVPXYL, LBH\n\
ENCCRY QBJA GB GUR FHESNPR BS URYY.",

"ABJ, VG'F BA GB GUR SVANY PUNCGRE BS QBBZ! -- VASREAB.",

"Rcvfbqr 3, VASREAB:\n\
-------------------",

"GUR YBNGUFBZR FCVQREQRZBA GUNG ZNFGREZVAQRQ GUR VAINFVBA BS GUR ZBBA ONFRF\n\
NAQ PNHFRQ FB ZHPU QRNGU UNF UNQ VGF NFF XVPXRQ SBE NYY GVZR.",

"N UVQQRA QBBEJNL BCRAF NAQ LBH RAGRE.  LBH'IR CEBIRA GBB GBHTU SBE URYY GB\n\
PBAGNVA, NAQ ABJ URYY NG YNFG CYNLF SNVE -- SBE LBH RZRETR SEBZ GUR QBBE GB\n\
FRR GUR TERRA SVRYQF BS RNEGU!  UBZR NG YNFG.",

"LBH JBAQRE JUNG'F ORRA UNCCRAVAT BA RNEGU JUVYR LBH JRER ONGGYVAT RIVY\n\
HAYRNFURQ. VG'F TBBQ GUNG AB URYY- FCNJA PBHYQ UNIR PBZR GUEBHTU GUNG QBBE\n\
JVGU LBH ...",

"Rcvfbqr 4, GUL SYRFU PBAFHZRQ:\n\
------------------------------",

"GUR FCVQRE ZNFGREZVAQ ZHFG UNIR FRAG SBEGU VGF YRTVBAF BS URYYFCNJA ORSBER\n\
LBHE SVANY PBASEBAGNGVBA JVGU GUNG GREEVOYR ORNFG SEBZ URYY. OHG LBH FGRCCRQ\n\
SBEJNEQ NAQ OEBHTUG SBEGU RGREANY QNZANGVBA NAQ FHSSREVAT HCBA GUR UBEQR NF N\n\
GEHR UREB JBHYQ VA GUR SNPR BS FBZRGUVAT FB RIVY.",

"ORFVQRF, FBZRBAR JNF TBAAN CNL SBE JUNG UNCCRARQ GB QNVFL, LBHE CRG ENOOVG.",

"OHG ABJ, LBH FRR FCERNQ ORSBER LBH ZBER CBGRAGVNY CNVA NAQ TVOOVGHQR NF N\n\
ANGVBA BS QRZBAF EHA NZBX VA BHE PVGVRF.",

"ARKG FGBC, URYY BA RNEGU!"
};

struct stream
{
  struct list_node list_fill;
  struct list_node list_flush;
  struct mutex* mutex;
  struct cond* cond_fill;
  struct cond* cond_flush;
};

struct buff
{
  struct list_node node;
  char scratch[1024];
};

static void*
read(void* arg)
{
  struct stream* stream = arg;
  size_t i = 0;
  ASSERT(stream);

  FOR_EACH(i, 0, sizeof(src_str)/sizeof(const char*)) {
    struct list_node* buff_node = NULL;
    struct buff* buff = NULL;

    mutex_lock(stream->mutex);
    if(is_list_empty(&stream->list_flush)) {
      cond_wait(stream->cond_flush, stream->mutex);
    }
    mutex_unlock(stream->mutex);

    buff_node = list_head(&stream->list_flush);
    buff = CONTAINER_OF(buff_node, struct buff, node);
    CHK(strcmp(buff->scratch, src_str[i]) == 0);
    printf("\n%s\n", buff->scratch);

    mutex_lock(stream->mutex);
    list_move_tail(buff_node, &stream->list_fill);
    mutex_unlock(stream->mutex);

    cond_broadcast(stream->cond_fill);
  }
  return NULL;
}

static void*
write(void* arg)
{
  struct stream* stream = arg;
  size_t i = 0;
  ASSERT(stream);

  FOR_EACH(i, 0, sizeof(src_str)/sizeof(const char*)) {
    struct list_node* buff_node = NULL;
    struct buff* buff = NULL;

    mutex_lock(stream->mutex);
    if(is_list_empty(&stream->list_fill)) {
      cond_wait(stream->cond_fill, stream->mutex);
    }
    mutex_unlock(stream->mutex);

    buff_node = list_head(&stream->list_fill);
    buff = CONTAINER_OF(buff_node, struct buff, node);

    ASSERT(sizeof(buff->scratch)/sizeof(char) > strlen(src_str[i]));
    strcpy(buff->scratch, src_str[i]);

    mutex_lock(stream->mutex);
    list_move_tail(buff_node, &stream->list_flush);
    mutex_unlock(stream->mutex);

    cond_broadcast(stream->cond_flush);
  }
  return NULL;
}

int
main(int argc, char** argv)
{
  struct buff buff[2];
  struct stream stream;
#if defined(OS_WINDOWS)
  HANDLE thread;
#else
  pthread_t thread;
#endif
  (void)argc, (void)argv;

  list_init(&stream.list_fill);
  list_init(&stream.list_flush);
  stream.mutex = mutex_create();
  CHK(stream.mutex != NULL);
  stream.cond_flush = cond_create();
  CHK(stream.cond_flush != NULL);
  stream.cond_fill = cond_create();
  CHK(stream.cond_fill != NULL);

  list_init(&buff[0].node);
  list_init(&buff[1].node);
  list_add(&stream.list_fill, &buff[0].node);
  list_add(&stream.list_fill, &buff[1].node);

#if defined(OS_WINDOWS)
  thread = (HANDLE)_beginthreadex(NULL, 0,
    (unsigned (__stdcall*)(void*))read, &stream, 0, NULL);
  CHK(thread != NULL);
  write(&stream);
  CHK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
  CloseHandle(thread);
#else
  CHK(pthread_create(&thread, NULL, read, &stream) == 0); /* Sub thread */
  write(&stream);
  CHK(pthread_join(thread, NULL) == 0);
#endif

  mutex_destroy(stream.mutex);
  cond_destroy(stream.cond_flush);
  cond_destroy(stream.cond_fill);
  return 0;
}
