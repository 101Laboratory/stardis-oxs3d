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

#ifndef SUVM_C_H
#define SUVM_C_H

#include "suvm_backend.h"
#include <rsys/rsys.h>

static FINLINE res_T
rtc_error_to_res_T(const enum RTCError err)
{
  switch(err) {
    case RTC_ERROR_NONE: return RES_OK;
    case RTC_ERROR_UNKNOWN: return RES_UNKNOWN_ERR;
    case RTC_ERROR_INVALID_ARGUMENT: return RES_BAD_ARG;
    case RTC_ERROR_INVALID_OPERATION: return RES_BAD_ARG;
    case RTC_ERROR_OUT_OF_MEMORY: return RES_MEM_ERR;
    case RTC_ERROR_UNSUPPORTED_CPU: return RES_BAD_ARG;
    case RTC_ERROR_CANCELLED: return RES_UNKNOWN_ERR;
    default: FATAL("Unreachable code\n"); break;
  }
}

static INLINE const char*
rtc_error_string(const enum RTCError err)
{
  const char* str = NULL;
  switch(err) {
    case RTC_ERROR_NONE: str = "No error"; break;
    case RTC_ERROR_UNKNOWN: str = "Unknown error"; break;
    case RTC_ERROR_INVALID_ARGUMENT: str = "Invalid argument"; break;
    case RTC_ERROR_INVALID_OPERATION: str = "Invalid operation"; break;
    case RTC_ERROR_OUT_OF_MEMORY: str = "Out of memory"; break;
    case RTC_ERROR_UNSUPPORTED_CPU: str = "Unsupported CPU"; break;
    case RTC_ERROR_CANCELLED: str = "Cancelled operation"; break;
    default: FATAL("Unreachable code\n"); break;
  }
  return str;
}

#endif /* SUVM_C_H */
