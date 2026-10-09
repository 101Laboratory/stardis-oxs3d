/* Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#ifndef SMSH_LOG_H
#define SMSH_LOG_H

#include <rsys/rsys.h>

#define MSG_INFO_PREFIX "Star-Mesh:\x1b[1m\x1b[32minfo\x1b[0m: "
#define MSG_ERROR_PREFIX "Star-Mesh:\x1b[1m\x1b[31merror\x1b[0m: "
#define MSG_WARNING_PREFIX "Star-Mesh:\x1b[1m\x1b[33mwarning\x1b[0m: "

struct smsh;
struct logger;

extern LOCAL_SYM res_T
setup_log_default
  (struct smsh* smsh);

/* Conditionally log a message on the LOG_OUTPUT stream of the smsh logger,
 * with respect to its verbose flag */
extern LOCAL_SYM void
log_info
  (const struct smsh* smsh,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
  __attribute((format(printf, 2, 3)))
#endif
;

/* Conditionally log a message on the LOG_ERROR stream of the smsh logger,
 * with respect to its verbose flag */
extern LOCAL_SYM void
log_err
  (const struct smsh* smsh,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
  __attribute((format(printf, 2, 3)))
#endif
;

/* Conditionally log a message on the LOG_WARNING stream of the smsh logger,
 * with respect to its verbose flag */
extern LOCAL_SYM void
log_warn
  (const struct smsh* smsh,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
    __attribute((format(printf, 2, 3)))
#endif
;

#endif /* SMSH_LOG_H */

