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

#include "mem_allocator.h"
#include "test_utils.h"
#include "text_reader.h"

#include <string.h>

static const char* text[] = {
  "Znxr rnpu cebtenz qb bar guvat jryy. Gb qb n arj wbo, ohvyq nserfu engure"
  "guna pbzcyvpngr byq cebtenzf ol nqqvat arj \"srngherf\"\n",
  "   \t\t # rzcgl yvar\n",
  "Rkcrpg gur bhgchg bs rirel cebtenz gb orpbzr gur vachg gb nabgure, nf lrg"
  "haxabja, cebtenz. Qba'g pyhggre bhgchg jvgu rkgenarbhf vasbezngvba. Nibvq"
  "fgevatragyl pbyhzane be ovanel vachg sbezngf. Qba'g vafvfg ba vagrenpgvir"
  "vachg.\n",
  "# pbzzrag\n",
  "Qrfvta naq ohvyq fbsgjner, rira bcrengvat flfgrzf, gb or gevrq rneyl, vqrnyyl"
  "jvguva jrrxf. Qba'g urfvgngr gb guebj njnl gur pyhzfl cnegf naq erohvyq gurz.\n",
  "\n",
  "Hfr gbbyf va cersrerapr gb hafxvyyrq uryc gb yvtugra n cebtenzzvat gnfx, rira"
  "vs lbh unir gb qrgbhe gb ohvyq gur gbbyf naq rkcrpg gb guebj fbzr bs gurz bhg"
  "nsgre lbh'ir svavfurq hfvat gurz.\n"
};
static const size_t nlines = sizeof(text)/sizeof(const char*);

static void
check_text_reader(struct txtrdr* txtrdr)
{
  size_t iline = 0;

  CHK(txtrdr_read_line(txtrdr) == RES_OK);
  while(txtrdr_get_line(txtrdr)) {

    /* Discard empty line */
    while(strcspn(text[iline], "#\n") == strspn(text[iline], " \t")) ++iline;

    CHK(!strncmp(txtrdr_get_line(txtrdr), text[iline], strcspn(text[iline], "#\n")));
    CHK(txtrdr_get_line_num(txtrdr) == iline+1);
    CHK(txtrdr_read_line(txtrdr) == RES_OK);
    ++iline;
  }
  CHK(txtrdr_get_line_num(txtrdr) == nlines);
  CHK(txtrdr_read_line(txtrdr) == RES_OK);
  CHK(txtrdr_get_line(txtrdr) == NULL);
  CHK(txtrdr_get_line_num(txtrdr) == nlines);
  CHK(txtrdr_read_line(txtrdr) == RES_OK);
  CHK(txtrdr_get_line(txtrdr) == NULL);
  txtrdr_set_line_num(txtrdr, nlines/2);
  CHK(txtrdr_get_line_num(txtrdr) == nlines/2);
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct txtrdr* txtrdr = NULL;
  size_t i;
  FILE* stream;
  const char* filename = "test_text_reader.txt";
  const char* stream_name = "my_stream";
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  /* Write the text into a stream */
  stream = fopen(filename, "w+");
  CHK(stream);
  FOR_EACH(i, 0, nlines) {
    const size_t len = strlen(text[i]);
    CHK(fwrite(text[i], 1, len, stream) == len);
  }

  rewind(stream);
  CHK(txtrdr_stream(NULL, stream, NULL, '#', &txtrdr) == RES_OK);
  check_text_reader(txtrdr);
  txtrdr_ref_get(txtrdr);
  txtrdr_ref_put(txtrdr);
  txtrdr_ref_put(txtrdr);

  rewind(stream);
  CHK(txtrdr_stream(&allocator, stream, stream_name, '#', &txtrdr) == RES_OK);
  CHK(!strcmp(txtrdr_get_name(txtrdr), stream_name));
  CHK(txtrdr_get_stream(txtrdr) == stream);
  check_text_reader(txtrdr);
  txtrdr_ref_put(txtrdr);

  CHK(txtrdr_file(&allocator, filename, '#', &txtrdr) == RES_OK);
  CHK(!strcmp(txtrdr_get_name(txtrdr), filename));
  check_text_reader(txtrdr);
  txtrdr_ref_put(txtrdr);

  stream = freopen(filename, "w+", stream);
  CHK(stream);
  fprintf(stream, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  fprintf(stream, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  rewind(stream);
  CHK(txtrdr_stream(&allocator, stream, filename, -1, &txtrdr) == RES_OK);
  CHK(txtrdr_read_line(txtrdr) == RES_OK);
  txtrdr_ref_put(txtrdr);

  stream = freopen(filename, "w+", stream);
  CHK(stream);
  fprintf(stream, "\r\n");
  rewind(stream);
  CHK(txtrdr_stream(&allocator, stream, filename, '#', &txtrdr) == RES_OK);
  CHK(txtrdr_read_line(txtrdr) == RES_OK);
  CHK(txtrdr_get_line(txtrdr) == NULL);
  txtrdr_ref_put(txtrdr);

  stream = freopen(filename, "w+", stream);
  CHK(stream);
  fprintf(stream, "   AAAA\nbbb\n");
  rewind(stream);
  CHK(txtrdr_stream(&allocator, stream, stream_name, 0, &txtrdr) == RES_OK);
  CHK(txtrdr_read_line(txtrdr) == RES_OK);
  CHK(txtrdr_get_line(txtrdr) != NULL);
  CHK(!strcmp(txtrdr_get_line(txtrdr), "   AAAA"));
  CHK(txtrdr_read_line(txtrdr) == RES_OK);
  CHK(txtrdr_get_line(txtrdr) != NULL);
  CHK(!strcmp(txtrdr_get_line(txtrdr), "bbb"));
  CHK(txtrdr_read_line(txtrdr) == RES_OK);
  CHK(txtrdr_get_line(txtrdr) == NULL);
  txtrdr_ref_put(txtrdr);

  fclose(stream);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
