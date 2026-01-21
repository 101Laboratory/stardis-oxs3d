/* Copyright (C) 2015, 2016, 2019, 2021, 2023, 2025 |Méso|Star> (contact@meso-star.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#define _POSIX_C_SOURCE 200112L /* strtok_r support */

#include "sstl.h"
#include "sstl_c.h"

#include <rsys/cstr.h>
#include <rsys/rsys.h>
#include <rsys/float3.h>
#include <rsys/stretchy_array.h>

#include <errno.h>
#include <stdio.h>

 /*******************************************************************************
  * Helper functions
  ******************************************************************************/
static res_T
file_type
(struct sstl* sstl,
	FILE* fp,
	const char* name,
	enum sstl_type* type)
{
	char buf[1024];
	size_t sz = 0;
	long fpos = 0;
	res_T res = RES_OK;

	ASSERT(sstl && fp && name && type);

	if (!file_is_seekable(fp)) {
		ERROR(sstl,
			"%s: the file refers to a pipe, a FIFO or a socket. "
			"Its type (i.e. ASCII or binary) cannot be defined on the fly.\n",
			name);
		res = RES_BAD_ARG;
		goto error;
	}

	if ((fpos = ftell(fp)) < 0) {
		ERROR(sstl, "%s: unable to query file position -- %s\n",
			name, strerror(errno));
		res = RES_IO_ERR;
		goto error;
	}

	/* Search for the NUL character in the first bytes of the file. If there is
	 * one, the file is assumed to be binary. This is a 'simple and stupid', yet
	 * robust method, used for example by some grep implementations */
	sz = fread(buf, 1, sizeof(buf), fp);
	if (memchr(buf, '\0', sz)) {
		*type = SSTL_BINARY;
	}
	else {
		*type = SSTL_ASCII;
	}

	if (fseek(fp, fpos, SEEK_SET) < 0) {
		ERROR(sstl, "%s: unable to set file position -- %s\n",
			name, strerror(errno));
		res = RES_IO_ERR;
		goto error;
	}

exit:
	return res;
error:
	goto exit;
}

static int
stream_is_seekable(FILE* fp)
{
	int fd = _fileno(fp);
	if (fd < 0)
		return 0;

	HANDLE h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE)
		return 0;

	DWORD type = GetFileType(h);
	return type == FILE_TYPE_DISK;
}

static res_T
load_stream
(struct sstl* sstl,
	FILE* fp,
	const char* name,
	enum sstl_type type)
{
	res_T res = RES_OK;
	ASSERT((unsigned)type <= SSTL_NONE__);

	if (!sstl || !fp || !name) { res = RES_BAD_ARG; goto error; }

	if (type == SSTL_NONE__) {
		/* NEW: 明确拒绝 unseekable stream */
		if (!stream_is_seekable(fp)) {
			res = RES_BAD_ARG;
			goto error;
		}

		res = file_type(sstl, fp, name, &type);
		if (res != RES_OK) goto error;
	}

	if ((res = str_set(&sstl->filename, name)) != RES_OK) {
		ERROR(sstl, "Error copying file name '%s' -- %s\n",
			name, res_to_cstr(res));
		goto error;
	}

	switch (type) {
	case SSTL_ASCII: res = load_stream_ascii(sstl, fp, name); break;
	case SSTL_BINARY: res = load_stream_binary(sstl, fp, name); break;
	default: FATAL("Unreachable code\n"); break;
	}
	if (res != RES_OK) goto error;

exit:
	return res;
error:
	goto exit;
}

static res_T
load(struct sstl* sstl, const char* filename, const enum sstl_type type)
{
	FILE* stream = NULL;
	res_T res = RES_OK;

	ASSERT((unsigned)type <= SSTL_NONE__);

	if (!sstl || !filename) { res = RES_BAD_ARG; goto error; }

	stream = fopen(filename, "r");
	if (!stream) {
		ERROR(sstl, "Error opening file %s -- %s\n", filename, strerror(errno));
		res = RES_IO_ERR;
		goto error;
	}

	res = load_stream(sstl, stream, filename, type);
	if (res != RES_OK) goto error;

exit:
	if (stream) CHK(fclose(stream) == 0);
	return res;
error:
	goto exit;
}

static void
sstl_release(ref_T* ref)
{
	struct sstl* sstl;
	ASSERT(ref);
	sstl = CONTAINER_OF(ref, struct sstl, ref);
	str_release(&sstl->filename);
	str_release(&sstl->name);
	htable_vertex_release(&sstl->vertex2id);
	sa_release(sstl->vertices);
	sa_release(sstl->normals);
	sa_release(sstl->indices);
	MEM_RM(sstl->allocator, sstl);
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
register_vertex(struct sstl* sstl, const float v[3])
{
	struct vertex vtx;
	unsigned* found = NULL;
	unsigned id = 0;
	res_T res = RES_OK;
	ASSERT(sstl && v);

	/* Check if the input vertex is already registered */
	f3_set(vtx.xyz, v);
	found = htable_vertex_find(&sstl->vertex2id, &vtx);

	if (found) { /* The vertex already exists */
		id = *found;

	}
	else { /* The vertex is a new one */
		id = (unsigned)sa_size(sstl->vertices) / 3;
		res = htable_vertex_set(&sstl->vertex2id, &vtx, &id);
		if (res != RES_OK) goto error;

		/* Add a new vertex */
		f3_set(sa_add(sstl->vertices, 3), vtx.xyz);
	}

	/* Register the vertex index */
	sa_push(sstl->indices, id);

exit:
	return res;
error:
	goto exit;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sstl_create
(struct logger* log,
	struct mem_allocator* mem_allocator,
	const int verbose,
	struct sstl** out_sstl)
{
	struct mem_allocator* allocator = NULL;
	struct logger* logger = NULL;
	struct sstl* sstl = NULL;
	res_T res = RES_OK;

	if (!out_sstl) {
		res = RES_BAD_ARG;
		goto error;
	}

	allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
	logger = log ? log : LOGGER_DEFAULT;

	sstl = MEM_CALLOC(allocator, 1, sizeof(struct sstl));
	if (!sstl) {
		if (verbose) {
			logger_print(logger, LOG_ERROR,
				"Couldn't allocate the Star-STL device.\n");
		}
		res = RES_MEM_ERR;
		goto error;
	}

	ref_init(&sstl->ref);
	sstl->allocator = allocator;
	sstl->logger = logger;
	sstl->verbose = verbose;
	htable_vertex_init(allocator, &sstl->vertex2id);
	str_init(allocator, &sstl->filename);
	str_init(allocator, &sstl->name);

exit:
	if (out_sstl) *out_sstl = sstl;
	return res;
error:
	if (sstl) {
		SSTL(ref_put(sstl));
		sstl = NULL;
	}
	goto exit;
}

res_T
sstl_ref_get(struct sstl* sstl)
{
	if (!sstl) return RES_BAD_ARG;
	ref_get(&sstl->ref);
	return RES_OK;
}

res_T
sstl_ref_put(struct sstl* sstl)
{
	if (!sstl) return RES_BAD_ARG;
	ref_put(&sstl->ref, sstl_release);
	return RES_OK;
}

res_T
sstl_load(struct sstl* sstl, const char* filename)
{
	return load(sstl, filename, SSTL_NONE__);
}

res_T
sstl_load_ascii(struct sstl* sstl, const char* filename)
{
	return load(sstl, filename, SSTL_ASCII);
}

res_T
sstl_load_binary(struct sstl* sstl, const char* filename)
{
	return load(sstl, filename, SSTL_BINARY);
}

res_T
sstl_load_stream(struct sstl* sstl, FILE* fp, const char* name)
{
	return load_stream(sstl, fp, name, SSTL_NONE__);
}

res_T
sstl_load_stream_ascii(struct sstl* sstl, FILE* fp, const char* name)
{
	return load_stream(sstl, fp, name, SSTL_ASCII);
}

res_T
sstl_load_stream_binary(struct sstl* sstl, FILE* fp, const char* name)
{
	return load_stream(sstl, fp, name, SSTL_BINARY);
}

res_T
sstl_get_desc(struct sstl* sstl, struct sstl_desc* desc)
{
	if (!sstl || !desc) return RES_BAD_ARG;

	ASSERT(sa_size(sstl->vertices) % 3 == 0);
	ASSERT(sa_size(sstl->normals) % 3 == 0);
	ASSERT(sa_size(sstl->indices) % 3 == 0);

	desc->filename = str_cget(&sstl->filename);
	desc->solid_name = str_len(&sstl->name) ? str_cget(&sstl->name) : NULL;
	desc->vertices_count = sa_size(sstl->vertices) / 3/*#coords*/;
	desc->triangles_count = sa_size(sstl->indices) / 3/*#ids*/;
	desc->vertices = sstl->vertices;
	desc->indices = sstl->indices;
	desc->normals = sstl->normals;
	desc->type = sstl->type;
	return RES_OK;
}
