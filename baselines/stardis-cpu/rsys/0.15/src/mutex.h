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

#ifndef MUTEX_H
#define MUTEX_H

#include "rsys.h"

BEGIN_DECLS

struct mutex;
RSYS_API struct mutex* mutex_create(void); /* NULL <=> error */
RSYS_API void mutex_destroy(struct mutex* mutex);
RSYS_API void mutex_lock(struct mutex* mutex);
RSYS_API void mutex_unlock(struct mutex* mutex);

struct mutex_rw;
RSYS_API struct mutex_rw* mutex_rw_create(void);/* NULL <=> error */
RSYS_API void mutex_rw_destroy(struct mutex_rw* mutex);
RSYS_API void mutex_rw_rlock(struct mutex_rw* mutex);
RSYS_API void mutex_rw_wlock(struct mutex_rw* mutex);
RSYS_API void mutex_rw_unlock(struct mutex_rw* mutex);

END_DECLS

#endif /* MUTEX_H */
