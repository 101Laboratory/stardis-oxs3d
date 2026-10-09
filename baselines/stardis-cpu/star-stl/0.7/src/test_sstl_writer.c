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

#define _POSIX_C_SOURCE 200112L /* fork */

#include "sstl.h"

#include <rsys/float3.h>
#include <rsys/logger.h>
#include <rsys/mem_allocator.h>

#include <string.h>
#include <stdlib.h>

#if defined(_WIN32)
#define pid_t int
#include <io.h>
#include <fcntl.h>
#include <process.h>
#include <windows.h>
#define pipe(fd) _pipe((fd), 4096, _O_BINARY)
#define fork()   win32_fork_not_supported
#define write    _write
#define close    _close
#else
#include <unistd.h> /* fork, pipe */
#endif

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
log_stream(const char* msg, void* ctx)
{
  ASSERT(msg);
  (void)msg, (void)ctx;
  printf("logger %s", msg);
}

static void
check_api(const enum sstl_type type)
{
  struct logger logger;
  struct mem_allocator allocator;
  struct sstl_writer_create_args args = SSTL_WRITER_CREATE_ARGS_DEFAULT;
  struct sstl_writer* writer = NULL;
  const char* filename = "test.stl";

  CHK(sstl_writer_create(NULL, &writer) == RES_BAD_ARG);
  CHK(sstl_writer_create(&args, NULL) == RES_BAD_ARG);
  CHK(sstl_writer_create(&args, &writer) == RES_BAD_ARG);

  args.filename = filename;
  args.type = type;
  args.verbose = 3;
  CHK(sstl_writer_create(&args, &writer) == RES_OK);

  CHK(sstl_write_facet(NULL, &SSTL_FACET_NULL) == RES_BAD_ARG);
  CHK(sstl_write_facet(writer, NULL) == RES_BAD_ARG);
  CHK(sstl_write_facet(writer, &SSTL_FACET_NULL) == RES_OK);

  CHK(sstl_writer_ref_get(NULL) == RES_BAD_ARG);
  CHK(sstl_writer_ref_get(writer) == RES_OK);
  CHK(sstl_writer_ref_put(NULL) == RES_BAD_ARG);
  CHK(sstl_writer_ref_put(writer) == RES_OK);
  CHK(sstl_writer_ref_put(writer) == RES_OK);

  args.triangles_count = 0;
  CHK(sstl_writer_create(&args, &writer) == RES_OK);
  CHK(sstl_write_facet(writer, &SSTL_FACET_NULL) == RES_BAD_OP);
  CHK(sstl_writer_ref_put(writer) == RES_OK);

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(logger_init(&allocator, &logger) == RES_OK);
  logger_set_stream(&logger, LOG_OUTPUT, log_stream, NULL);
  logger_set_stream(&logger, LOG_ERROR, log_stream, NULL);
  logger_set_stream(&logger, LOG_WARNING, log_stream, NULL);

  args.allocator = &allocator;
  args.logger = &logger;
  args.triangles_count = 1;
  CHK(sstl_writer_create(&args, &writer) == RES_OK);
  CHK(sstl_write_facet(writer, &SSTL_FACET_NULL) == RES_OK);
  CHK(sstl_write_facet(writer, &SSTL_FACET_NULL) == RES_BAD_OP);
  CHK(sstl_writer_ref_put(writer) == RES_OK);

  logger_release(&logger);
  CHK(MEM_ALLOCATED_SIZE(&allocator) == 0);
  mem_shutdown_proxy_allocator(&allocator);
}

static void
check_1_triangle(struct sstl* sstl, const enum sstl_type type)
{
  struct sstl_desc desc = SSTL_DESC_NULL;
  struct sstl_facet facet = SSTL_FACET_NULL;
  struct sstl_writer_create_args args = SSTL_WRITER_CREATE_ARGS_DEFAULT;
  struct sstl_writer* writer = NULL;

  float v[3] = {0,0,0};
  const char* filename = "1_triangle.stl";

  args.filename = filename;
  args.type = type;
  args.verbose = 3;
  CHK(sstl_writer_create(&args, &writer) == RES_OK);

  f3_splat(facet.normal, 0); /* Let sstl calculate it automatically */
  f3(facet.vertices[0], 0.f, 0.f, 0.f);
  f3(facet.vertices[1], 1.f, 0.f, 0.f);
  f3(facet.vertices[2], 0.f, 0.f, 1.f);
  CHK(sstl_write_facet(writer, &facet) == RES_OK);

  CHK(sstl_writer_ref_put(writer) == RES_OK);

  CHK(sstl_load(sstl, filename) == RES_OK);
  CHK(sstl_get_desc(sstl, &desc) == RES_OK);

  CHK(desc.type == type);
  CHK(!strcmp(desc.filename, filename));
  CHK(desc.solid_name == NULL);
  CHK(desc.vertices_count == 3);
  CHK(desc.triangles_count == 1);
  CHK(desc.indices[0] == 0);
  CHK(desc.indices[1] == 1);
  CHK(desc.indices[2] == 2);
  CHK(f3_eq(desc.vertices + 0*3, f3(v, 0.f, 0.f, 0.f)) == 1);
  CHK(f3_eq(desc.vertices + 1*3, f3(v, 1.f, 0.f, 0.f)) == 1);
  CHK(f3_eq(desc.vertices + 2*3, f3(v, 0.f, 0.f, 1.f)) == 1);
  CHK(f3_eq(desc.normals, f3(v, 0.f, -1.f, 0.f)) == 1);

  /* The triangle is missing. A warning is signalled but no error is returned:
   * author finalization is assumed to always succeed. Partial finalization
   * corrupts the output, and it would be too hasardous to try returning to the
   * previous state. Finally, detecting a finalization error prevents the writer
   * from being released, adding a memory leak to the finalization error, which
   * could be impossible to resolve. */
  args.filename = filename;
  args.type = SSTL_BINARY;
  args.triangles_count = 1;
  args.verbose = 3;
  CHK(sstl_writer_create(&args, &writer) == RES_OK);
  CHK(sstl_writer_ref_put(writer) == RES_OK);
}

static void
check_tetrahedron(struct sstl* sstl, const enum sstl_type type)
{
  struct sstl_desc desc = SSTL_DESC_NULL;
  struct sstl_facet facet = SSTL_FACET_NULL;
  struct sstl_writer_create_args args = SSTL_WRITER_CREATE_ARGS_DEFAULT;
  struct sstl_writer* writer = NULL;

  float v[3] = {0,0,0};
  FILE* fp = NULL;
  const char* filename = "Tetrahedron";

  CHK((fp = tmpfile()) != NULL);
  args.filename = filename;
  args.stream = fp;
  args.type = type;
  args.solid_name = "cube corner";
  args.verbose = 3;
  CHK(sstl_writer_create(&args, &writer) == RES_OK);

  f3(facet.normal, 0.f,-1.f, 0.f);
  f3(facet.vertices[0], 0.0f, 0.0f, 0.0f);
  f3(facet.vertices[1], 0.1f, 0.0f, 0.0f);
  f3(facet.vertices[2], 0.0f, 0.0f, 0.1f);
  CHK(sstl_write_facet(writer, &facet) == RES_OK);

  f3(facet.normal, 0.f, 0.f, -1.f);
  f3(facet.vertices[0], 0.0f, 0.0f, 0.0f);
  f3(facet.vertices[1], 0.0f, 0.1f, 0.0f);
  f3(facet.vertices[2], 0.1f, 0.0f, 0.0f);
  CHK(sstl_write_facet(writer, &facet) == RES_OK);

  f3(facet.normal,-1.f, 0.f, 0.f);
  f3(facet.vertices[0], 0.0f, 0.0f, 0.0f);
  f3(facet.vertices[1], 0.0f, 0.0f, 0.1f);
  f3(facet.vertices[2], 0.0f, 0.1f, 0.0f);
  CHK(sstl_write_facet(writer, &facet) == RES_OK);

  f3(facet.normal, 0.577f, 0.577f, 0.577f);
  f3(facet.vertices[0], 0.1f, 0.0f, 0.0f);
  f3(facet.vertices[1], 0.0f, 0.1f, 0.0f);
  f3(facet.vertices[2], 0.0f, 0.0f, 0.1f);
  CHK(sstl_write_facet(writer, &facet) == RES_OK);

  CHK(sstl_writer_ref_put(writer) == RES_OK);

  rewind(fp);
  CHK(sstl_load_stream(sstl, fp, filename) == RES_OK);
  CHK(fclose(fp) == 0);

  CHK(sstl_get_desc(sstl, &desc) == RES_OK);
  CHK(!strcmp(desc.filename, filename));
  CHK(type == SSTL_BINARY || !strcmp(desc.solid_name, "cube corner"));
  CHK(desc.type == type);
  CHK(desc.vertices_count == 4);
  CHK(desc.triangles_count == 4);

  CHK(f3_eq(desc.vertices + desc.indices[0]*3, f3(v, 0.0f, 0.0f, 0.0f)) == 1);
  CHK(f3_eq(desc.vertices + desc.indices[1]*3, f3(v, 0.1f, 0.0f, 0.0f)) == 1);
  CHK(f3_eq(desc.vertices + desc.indices[2]*3, f3(v, 0.0f, 0.0f, 0.1f)) == 1);
  CHK(f3_eq(desc.vertices + desc.indices[3]*3, f3(v, 0.0f, 0.0f, 0.0f)) == 1);
  CHK(f3_eq(desc.vertices + desc.indices[4]*3, f3(v, 0.0f, 0.1f, 0.0f)) == 1);
  CHK(f3_eq(desc.vertices + desc.indices[5]*3, f3(v, 0.1f, 0.0f, 0.0f)) == 1);
  CHK(f3_eq(desc.vertices + desc.indices[6]*3, f3(v, 0.0f, 0.0f, 0.0f)) == 1);
  CHK(f3_eq(desc.vertices + desc.indices[7]*3, f3(v, 0.0f, 0.0f, 0.1f)) == 1);
  CHK(f3_eq(desc.vertices + desc.indices[8]*3, f3(v, 0.0f, 0.1f, 0.0f)) == 1);
  CHK(f3_eq(desc.vertices + desc.indices[9]*3, f3(v, 0.1f, 0.0f, 0.0f)) == 1);
  CHK(f3_eq(desc.vertices + desc.indices[10]*3, f3(v, 0.0f, 0.1f, 0.0f)) == 1);
  CHK(f3_eq(desc.vertices + desc.indices[11]*3, f3(v, 0.0f, 0.0f, 0.1f)) == 1);

  CHK(f3_eq(desc.normals + 0*3, f3(v, 0.f,-1.f, 0.f)) == 1);
  CHK(f3_eq(desc.normals + 1*3, f3(v, 0.f, 0.f,-1.f)) == 1);
  CHK(f3_eq(desc.normals + 2*3, f3(v,-1.f, 0.f, 0.f)) == 1);
  f3_normalize(v, f3(v, 1.f, 1.f, 1.f));
  CHK(f3_eq_eps(desc.normals + 3*3, v, 1.e-6f) == 1);
}

static void
process_write(const enum sstl_type type, FILE* fp, const char* filename)
{
  struct sstl_writer_create_args args = SSTL_WRITER_CREATE_ARGS_DEFAULT;
  struct sstl_facet facet = SSTL_FACET_NULL;
  struct sstl_writer* writer = NULL;

  args.solid_name = "Triangle";
  args.filename = filename;
  args.stream = fp;
  args.type = type;
  args.verbose = 3;
  args.triangles_count = 1;

  CHK(sstl_writer_create(&args, &writer) == RES_OK);

  f3(facet.normal, 0.f,-1.f, 0.f);
  f3(facet.vertices[0], 1.f, 0.f, 0.f);
  f3(facet.vertices[1], 0.f, 0.f, 1.f);
  f3(facet.vertices[2], 0.f, 0.f, 0.f);
  CHK(sstl_write_facet(writer, &facet) == RES_OK);

  CHK(sstl_writer_ref_put(writer) == RES_OK);
}

static void
process_read
  (struct sstl* sstl,
   const enum sstl_type type,
   FILE* fp,
   const char* filename)
{
  struct sstl_desc desc = SSTL_DESC_NULL;
  float v[3];

  switch(type) {
    case SSTL_ASCII:
      CHK(sstl_load_stream_ascii(sstl, fp, filename) == RES_OK);
      break;
    case SSTL_BINARY:
      CHK(sstl_load_stream_binary(sstl, fp, filename) == RES_OK);
      break;
    default: FATAL("Unreachable code\n"); break;
  }

  CHK(sstl_get_desc(sstl, &desc) == RES_OK);
  CHK(desc.type == type);
  CHK(!strcmp(desc.filename, filename));
  CHK(type == SSTL_ASCII || desc.solid_name == NULL);
  CHK(type == SSTL_BINARY || !strcmp(desc.solid_name, "Triangle"));
  CHK(desc.vertices_count == 3);
  CHK(desc.triangles_count == 1);
  CHK(desc.indices[0] == 0);
  CHK(desc.indices[1] == 1);
  CHK(desc.indices[2] == 2);
  CHK(f3_eq(desc.vertices + 0*3, f3(v, 1.f, 0.f, 0.f)) == 1);
  CHK(f3_eq(desc.vertices + 1*3, f3(v, 0.f, 0.f, 1.f)) == 1);
  CHK(f3_eq(desc.vertices + 2*3, f3(v, 0.f, 0.f, 0.f)) == 1);
  CHK(f3_eq(desc.normals, f3(v, 0.f, -1.f, 0.f)) == 1);
}

#if defined(_WIN32)

typedef void (*child_process_func)(void*);

static void
run_pipe_writer(
    int fd[2],
    child_process_func fn,
    void* arg,
    HANDLE* out_thread)
{
    uintptr_t th;

    /* pipe 已在调用方创建，不要在这里重复创建！*/
    /* CHK(_pipe(fd, 4096, _O_BINARY) == 0); */  /* BUG: 会覆盖传入的 fd */

    th = _beginthreadex(
        NULL, 0,
        (unsigned(__stdcall*)(void*))fn,
        arg,
        0, NULL);
    CHK(th != 0);

    CHK(_close(fd[1]) == 0);

    if (out_thread)
        *out_thread = (HANDLE)th;
    else
        CloseHandle((HANDLE)th);
}

struct writer_args {
    int write_fd;
    enum sstl_type type;
    const char* filename;
};

static void
child_write_stl(void* arg)
{
    struct writer_args* a = arg;
    FILE* fp = NULL;

    fp = _fdopen(a->write_fd, "wb");
    CHK(fp != NULL);

    process_write(a->type, fp, a->filename);

    CHK(fclose(fp) == 0);
}

static void
check_no_seekable_file(struct sstl* sstl, const enum sstl_type type)
{
    int fd[2];
    FILE* fp = NULL;
    HANDLE th = NULL;
    const char* filename = "Pipe";
    struct writer_args* args = NULL;

    CHK(_pipe(fd, 4096, _O_BINARY) == 0);

    args = malloc(sizeof(*args));
    CHK(args != NULL);
    args->write_fd = _dup(fd[1]);
    args->type = type;
    args->filename = filename;

    run_pipe_writer(fd, child_write_stl, args, &th);

    fp = _fdopen(fd[0], "rb");
    CHK(fp != NULL);

    process_read(sstl, type, fp, filename);
    CHK(fclose(fp) == 0);

    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    free(args);
}

#else

static void
check_no_seekable_file(struct sstl* sstl, const enum sstl_type type)
{
  int fd[2] = {0,0};
  FILE* fp = NULL;
  const char* filename = "Pipe";
  pid_t pid = 0;

  CHK(pipe(fd) == 0);
  CHK((pid = fork()) != -1);

  if(pid == 0) {  /* Child process */
    CHK(close(fd[0]) == 0); /* Close the unused input stream */
    CHK(sstl_ref_put(sstl) == RES_OK); /* Release the unused sstl */
    CHK((fp = fdopen(fd[1], "w")) != NULL);
    process_write(type, fp, filename);
    CHK(fclose(fp) == 0);
    exit(0);

  } else { /* Parent process */
    CHK(close(fd[1]) == 0); /* Close the unused output stream */
    CHK(fp = fdopen(fd[0], "r"));
    process_read(sstl, type, fp, filename);
    CHK(fclose(fp) == 0);
  }
}

#endif

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sstl* sstl = NULL;
  (void)argc, (void)argv;

  CHK(sstl_create(NULL, NULL, 3, &sstl) == RES_OK);

  check_api(SSTL_ASCII);
  check_api(SSTL_BINARY);
  check_1_triangle(sstl, SSTL_ASCII);
  check_1_triangle(sstl, SSTL_BINARY);
  check_tetrahedron(sstl, SSTL_ASCII);
  check_tetrahedron(sstl, SSTL_BINARY);
  check_no_seekable_file(sstl, SSTL_ASCII);
  check_no_seekable_file(sstl, SSTL_BINARY);

  CHK(sstl_ref_put(sstl) == RES_OK);
  CHK(mem_allocated_size() == 0);
  return 0;
}
