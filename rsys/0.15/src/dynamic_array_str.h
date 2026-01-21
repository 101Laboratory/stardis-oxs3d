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

#ifndef DYNAMIC_ARRAY_STR_H
#define DYNAMIC_ARRAY_STR_H

#include "dynamic_array.h"
#include "str.h"

#define DARRAY_NAME str
#define DARRAY_DATA struct str
#define DARRAY_FUNCTOR_INIT str_init
#define DARRAY_FUNCTOR_COPY str_copy
#define DARRAY_FUNCTOR_RELEASE str_release
#define DARRAY_FUNCTOR_COPY_AND_RELEASE str_copy_and_release
#include "dynamic_array.h"

#endif /* DYNAMIC_ARRAY_STR_H */
