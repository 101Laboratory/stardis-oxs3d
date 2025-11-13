/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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

#ifndef SDIS_LOG_H
#define SDIS_LOG_H

#include <rsys/rsys.h>

/* By default assume messages are printed in a VT100-like terminal emulator */
#define MSG_INFO_PREFIX "stardis-solver (\x1b[1m\x1b[32minfo\x1b[0m): "
#define MSG_ERROR_PREFIX "stardis-solver (\x1b[1m\x1b[31merror\x1b[0m): "
#define MSG_WARNING_PREFIX "stardis-solver (\x1b[1m\x1b[33mwarning\x1b[0m): "

/* Plain text message prefixes */
#define MSG_INFO_PREFIX_PLAIN_TEXT "stardis-solver (info): "
#define MSG_ERROR_PREFIX_PLAIN_TEXT "stardis-solver (error): "
#define MSG_WARNING_PREFIX_PLAIN_TEXT "stardis-solver (warning): "

struct sdis_device;

extern LOCAL_SYM res_T
setup_log_default
  (struct sdis_device* dev);

/* Conditionally log a message on the LOG_OUTPUT stream of the sdis logger,
 * with respect to its verbose flag */
extern LOCAL_SYM void
log_info
  (const struct sdis_device* dev,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
  __attribute((format(printf, 2, 3)))
#endif
;

/* Conditionally log a message on the LOG_ERROR stream of the sdis logger,
 * with respect to its verbose flag */
extern LOCAL_SYM void
log_err
  (const struct sdis_device* dev,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
  __attribute((format(printf, 2, 3)))
#endif
;

/* Conditionally log a message on the LOG_WARNING stream of the sdis logger,
 * with respect to its verbose flag */
extern LOCAL_SYM void
log_warn
  (const struct sdis_device* dev,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
    __attribute((format(printf, 2, 3)))
#endif
;

#endif /* SDIS_LOG_H */
