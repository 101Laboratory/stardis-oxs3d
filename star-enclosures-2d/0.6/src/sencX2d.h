/* Copyright (C) 2018-2021, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef SENC_X_2D_H
#define SENC_X_2D_H

#if !defined(SENCXD_DIM) || (SENCXD_DIM != 2 && SENCXD_DIM != 3)
#error "SENCXD_DIM must be defined; admissible values are 2 and 3"
#endif

#include <star/senc2d.h>

/* Star-enclosures-XD macros generic to the SENCXD_DIM */
#ifndef SENCXD
#define SENCXD CONCAT(CONCAT(SENC, SENCXD_DIM), D)
#endif
#ifndef sencXd
#define sencXd(Name) CONCAT(CONCAT(CONCAT(senc, SENCXD_DIM), d_), Name)
#endif
#ifndef SENCXD_
#define SENCXD_(Name) CONCAT(CONCAT(CONCAT(SENC, SENCXD_DIM), D_), Name)
#endif

/* Function names that require additional dedicated macros */
#define senc2d_scene_get_primitives_count senc2d_scene_get_segments_count
#define senc2d_scene_get_primitive senc2d_scene_get_segment
#define senc2d_scene_get_primitive_media senc2d_scene_get_segment_media
#define senc2d_scene_get_primitive_enclosures senc2d_scene_get_segment_enclosures
#define senc2d_enclosure_get_primitive senc2d_enclosure_get_segment
#define senc2d_enclosure_get_primitive_id senc2d_enclosure_get_segment_id

#endif /* SENC_X_2D_H */
