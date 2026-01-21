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

#ifndef TEXT_READER_H
#define TEXT_READER_H

#include "rsys.h"

struct txtrdr;
struct mem_allocator;

BEGIN_DECLS

RSYS_API res_T
txtrdr_file
  (struct mem_allocator* allocator, /* May be NULL <=> default allocator */
   const char* filename,
   const char comment, /* Char preceeding a comment. 0 <=> no comment char */
   struct txtrdr** txtrdr);

RSYS_API res_T
txtrdr_stream
  (struct mem_allocator* allocator, /* May be NULL <=> default allocator */
   FILE* stream,
   const char* name, /* Stream name. May be NULL */
   const char comment, /* Char preceeding a comment. 0 <=> no comment char */
   struct txtrdr** txtrdr);

RSYS_API void
txtrdr_ref_get
  (struct txtrdr* txtrdr);

RSYS_API void
txtrdr_ref_put
  (struct txtrdr* txtrdr);

/* Read a non empty line, i.e. line with text that is not a comment. The new
 * line character as well as the comments are skipped from the reading. A
 * returned value different of RES_OK means than an error occurs while reading
 * the stream. Note that reaching the end of file is not an error and thus that
 * no error is returned if the end of file is reached, even though nothing was
 * read. In this case the returned line is simply NULL. */
RSYS_API res_T
txtrdr_read_line
  (struct txtrdr* txtrdr);

RSYS_API char*
txtrdr_get_line
  (struct txtrdr* txtrdr);

RSYS_API const char*
txtrdr_get_cline
  (const struct txtrdr* txtrdr);

/* Return the number of read lines */
RSYS_API size_t
txtrdr_get_line_num
  (const struct txtrdr* txtrdr);

/* Overwrite the internal line number */
RSYS_API void
txtrdr_set_line_num
  (struct txtrdr* txtrdr,
   const size_t nlines);

RSYS_API const char*
txtrdr_get_name
  (const struct txtrdr* txtrdr);

/* Return the stream currently read. Note that any modification of the returned
 * stream will affect the text reader */
RSYS_API FILE*
txtrdr_get_stream
  (const struct txtrdr* txtrdr);

END_DECLS

#endif /* TEXT_READER_H */
