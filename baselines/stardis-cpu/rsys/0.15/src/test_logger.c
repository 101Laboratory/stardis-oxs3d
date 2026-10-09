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

#include "logger.h"
#include "test_utils.h"

static void
func_stream_out(const char* fmt, void* data)
{
  *(int*)data += 1;
  CHK(strcmp(fmt, "output") ==  0);
}

static void
func_stream_out_std(const char* fmt, void* data)
{
  (void)data;
  printf("%s\n", fmt);
}

static void
func_stream_err(const char* fmt, void* data)
{
  *(int*)data += 1;
  CHK(strcmp(fmt, "error") ==  0);
}

static void
func_stream_warn(const char* fmt, void* data)
{
  *(int*)data += 1;
  CHK(strcmp( fmt, "warning" ) ==  0);
}

static void
test_vprint(struct logger* logger, const char* fmt, ...)
{
  va_list vargs_list;
  CHK(logger != NULL);
  CHK(fmt != NULL);

  va_start(vargs_list, fmt);
  logger_vprint(logger, LOG_OUTPUT, fmt, vargs_list);
  va_end(vargs_list);
}

int
main(int argc, char** argv)
{
  int i = 0;
  struct mem_allocator allocator_proxy;
  struct logger logger;
  int istream_out = 0;
  int istream_err = 0;
  int istream_warn = 0;
  (void)argc, (void)argv;

  mem_init_proxy_allocator( &allocator_proxy, &mem_default_allocator );

  FOR_EACH( i, 0, 10 ) {
    logger_print(LOGGER_DEFAULT, LOG_OUTPUT, "Output: %d\n", i);
    logger_print(LOGGER_DEFAULT, LOG_ERROR, "Error: %.10f\n", (float)i);
    logger_print(LOGGER_DEFAULT, LOG_WARNING, "Warn: hop%c\n", (char)('a'+i));
  }

  logger_init(&allocator_proxy, &logger);
  logger_print(&logger, LOG_OUTPUT, "out%s", "put");
  CHK(istream_out ==  0);
  CHK(istream_err ==  0);
  CHK(istream_warn ==  0);

  CHK(logger_has_stream(&logger, LOG_OUTPUT) ==  0);
  CHK(logger_has_stream(&logger, LOG_ERROR) ==  0);
  CHK(logger_has_stream(&logger, LOG_WARNING) ==  0);

  logger_set_stream(&logger, LOG_OUTPUT, func_stream_out, &istream_out);
  CHK(logger_has_stream(&logger, LOG_OUTPUT) ==  1);
  logger_print(&logger, LOG_OUTPUT, "output");
  CHK(istream_out ==  1);
  CHK(istream_err ==  0);
  CHK(istream_warn ==  0);

  logger_set_stream(&logger, LOG_ERROR, func_stream_err, &istream_err);
  logger_set_stream(&logger, LOG_WARNING, func_stream_warn, &istream_warn);
  CHK(logger_has_stream(&logger, LOG_OUTPUT) ==  1);
  CHK(logger_has_stream(&logger, LOG_ERROR) ==  1);
  CHK(logger_has_stream(&logger, LOG_WARNING) ==  1);
  logger_print(&logger, LOG_OUTPUT, "output");
  CHK(istream_out ==  2);
  CHK(istream_err ==  0);
  CHK(istream_warn ==  0);
  logger_print(&logger, LOG_ERROR, "error");
  CHK(istream_out ==  2);
  CHK(istream_err ==  1);
  CHK(istream_warn ==  0);
  logger_print(&logger, LOG_WARNING, "war%c%s", 'n', "ing");
  CHK(istream_out ==  2);
  CHK(istream_err ==  1);
  CHK(istream_warn ==  1);

  logger_set_stream(&logger, LOG_OUTPUT, func_stream_out, &istream_out);
  logger_print(&logger, LOG_OUTPUT, "output");
  CHK(istream_out ==  3);
  CHK(istream_err ==  1);
  CHK(istream_warn ==  1);

  logger_print
    ( &logger, LOG_OUTPUT, "%c%c%c%c%c%c", 'o', 'u' ,'t', 'p', 'u', 't' );
  CHK(istream_out ==  4);
  CHK(istream_err ==  1);
  CHK(istream_warn ==  1);
  logger_print(&logger, LOG_ERROR, "error");
  CHK(istream_out ==  4);
  CHK(istream_err ==  2);
  CHK(istream_warn ==  1);
  logger_print(&logger, LOG_WARNING, "warning");
  CHK(istream_out ==  4);
  CHK(istream_err ==  2);
  CHK(istream_warn ==  2);
  logger_set_stream(&logger, LOG_ERROR, NULL, NULL);
  logger_print(&logger, LOG_ERROR, "error");
  CHK(istream_out ==  4);
  CHK(istream_err ==  2);
  CHK(istream_warn ==  2);

  logger_clear(&logger);
  CHK(logger_has_stream(&logger, LOG_OUTPUT) ==  0);
  CHK(logger_has_stream(&logger, LOG_ERROR) ==  0);
  CHK(logger_has_stream(&logger, LOG_WARNING) ==  0);

  istream_out = istream_err = istream_warn = 0;
  logger_set_stream(&logger, LOG_OUTPUT, func_stream_out, &istream_out);
  logger_set_stream(&logger, LOG_ERROR, func_stream_out, &istream_out);
  logger_set_stream(&logger, LOG_WARNING, func_stream_out, &istream_out);
  logger_print(&logger, LOG_OUTPUT, "output");
  CHK(istream_out ==  1);
  CHK(istream_err ==  0);
  CHK(istream_warn ==  0);
  logger_print(&logger, LOG_ERROR, "output");
  CHK(istream_out ==  2);
  CHK(istream_err ==  0);
  CHK(istream_warn ==  0);
  logger_print(&logger, LOG_WARNING, "output");
  CHK(istream_out ==  3);
  CHK(istream_err ==  0);
  CHK(istream_warn ==  0);

  logger_set_stream(&logger, LOG_WARNING, NULL, NULL);
  logger_print(&logger, LOG_WARNING, "output");
  CHK(istream_out ==  3);
  CHK(istream_err ==  0);
  CHK(istream_warn ==  0);
  logger_print(&logger, LOG_ERROR, "output");
  logger_print(&logger, LOG_OUTPUT, "output");
  CHK(istream_out ==  5);
  CHK(istream_err ==  0);
  CHK(istream_warn ==  0);

  logger_set_stream(&logger, LOG_OUTPUT, NULL, NULL);
  logger_set_stream(&logger, LOG_OUTPUT, NULL, NULL);
  logger_print(&logger, LOG_WARNING, "output");
  logger_print(&logger, LOG_OUTPUT, "output");
  CHK(istream_out ==  5);
  CHK(istream_err ==  0);
  CHK(istream_warn ==  0);
  logger_print(&logger, LOG_ERROR, "output");
  CHK(istream_out ==  6);
  CHK(istream_err ==  0);
  CHK(istream_warn ==  0);

  logger_set_stream(&logger, LOG_OUTPUT, func_stream_out, &istream_out);
  logger_set_stream(&logger, LOG_WARNING, func_stream_out, &istream_out);
  logger_print(&logger, LOG_OUTPUT, "output");
  logger_print(&logger, LOG_ERROR, "output");
  logger_print(&logger, LOG_WARNING, "output");
  CHK(istream_out ==  9);
  CHK(istream_err ==  0);
  CHK(istream_warn ==  0);
  logger_clear(&logger);
  logger_print(&logger, LOG_OUTPUT, "output");
  logger_print(&logger, LOG_ERROR, "output");
  logger_print(&logger, LOG_WARNING, "output");
  CHK(istream_out ==  9);
  CHK(istream_err ==  0);
  CHK(istream_warn ==  0);

  logger_set_stream(&logger, LOG_OUTPUT, func_stream_out_std, NULL);
  logger_print
    (&logger, LOG_OUTPUT, "%s%s%s%s",
"Rcvfbqr 1, XARR-QRRC VA GUR QRNQ:\n\
---------------------------------\n\n",
"BAPR LBH ORNG GUR OVT ONQNFFRF NAQ PYRNA BHG GUR ZBBA ONFR LBH'ER FHCCBFRQ GB\n\
JVA, NERA'G LBH? NERA'G LBH? JURER'F LBHE SNG ERJNEQ NAQ GVPXRG UBZR? JUNG\n\
GUR URYY VF GUVF? VG'F ABG FHCCBFRQ GB RAQ GUVF JNL!\n\n",
"VG FGVAXF YVXR EBGGRA ZRNG, OHG YBBXF YVXR GUR YBFG QRVZBF ONFR. YBBXF YVXR\n\
LBH'ER FGHPX BA GUR FUBERF BS URYY. GUR BAYL JNL BHG VF GUEBHTU.\n\n",
"GB PBAGVAHR GUR QBBZ RKCREVRAPR, CYNL GUR FUBERF BS URYY NAQ VGF NZNMVAT\n\
FRDHRY, VASREAB!\n");

  test_vprint(&logger, "%s%s%s%s",
"Rcvfbqr 2, GUR FUBERF BS URYY:\n\
------------------------------\n\n",
"LBH'IR QBAR VG! GUR UVQRBHF PLORE- QRZBA YBEQ GUNG EHYRQ GUR YBFG QRVZBF ZBBA\n\
ONFR UNF ORRA FYNVA NAQ LBH NER GEVHZCUNAG! OHG ... JURER NER LBH? LBH\n\
PYNZORE GB GUR RQTR BS GUR ZBBA NAQ YBBX QBJA GB FRR GUR NJSHY GEHGU.\n\n",
"QRVZBF SYBNGF NOBIR URYY VGFRYS!  LBH'IR ARIRE URNEQ BS NALBAR RFPNCVAT SEBZ\n\
URYY, OHG LBH'YY ZNXR GUR ONFGNEQF FBEEL GURL RIRE URNEQ BS LBH! DHVPXYL, LBH\n\
ENCCRY QBJA GB GUR FHESNPR BS URYY.\n\n",
"ABJ, VG'F BA GB GUR SVANY PUNCGRE BS QBBZ! -- VASREAB.\n");

  logger_release(&logger);

  check_memory_allocator(&allocator_proxy);
  mem_shutdown_proxy_allocator(&allocator_proxy);
  CHK(mem_allocated_size() ==  0);
  return 0;
}
