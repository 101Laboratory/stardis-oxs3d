/* Copyright (C) 2021-2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef SCAM_LOG_H
#define SCAM_LOG_H

#include <rsys/rsys.h>

#define MSG_INFO_PREFIX "Star-Camera:\x1b[1m\x1b[32minfo\x1b[0m: "
#define MSG_ERROR_PREFIX "Star-Camera:\x1b[1m\x1b[31merror\x1b[0m: "
#define MSG_WARNING_PREFIX "Star-Camera:\x1b[1m\x1b[33mwarning\x1b[0m: "

extern LOCAL_SYM res_T
setup_log_default
  (struct scam* cam);

/* Conditionally log a message on the LOG_OUTPUT stream of the atrstm logger,
 * with respect to its verbose flag */
extern LOCAL_SYM void
log_info
  (const struct scam* cam,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
  __attribute((format(printf, 2, 3)))
#endif
;

/* Conditionally log a message on the LOG_ERROR stream of the scam logger,
 * with respect to its verbose flag */
extern LOCAL_SYM void
log_err
  (const struct scam* cam,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
  __attribute((format(printf, 2, 3)))
#endif
;

/* Conditionally log a message on the LOG_WARNING stream of the scam logger,
 * with respect to its verbose flag */
extern LOCAL_SYM void
log_warn
  (const struct scam* cam,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
    __attribute((format(printf, 2, 3)))
#endif
;

#endif /* SCAM_LOG_H */
