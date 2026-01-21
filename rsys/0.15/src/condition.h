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

#ifndef CONDITION_H
#define CONDITION_H

#include "rsys.h"
#include "mutex.h"

struct cond;
struct mutex;

RSYS_API struct cond* cond_create(void); /* NULL <=> error */
RSYS_API void cond_destroy(struct cond* cond);
RSYS_API void cond_wait(struct cond* cond, struct mutex* mutex);
RSYS_API void cond_signal(struct cond* cond);
RSYS_API void cond_broadcast(struct cond* cond);

#endif /* CONDITION_H */
