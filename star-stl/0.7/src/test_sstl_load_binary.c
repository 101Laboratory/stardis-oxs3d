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
#include <rsys/mem_allocator.h>

#include <string.h>
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
check_api(struct sstl* sstl)
{
  const char header[80] = {0};
  const uint32_t ntris = 0;
  const char* filename = "test.stl";
  FILE* fp = NULL;
  struct sstl_desc desc = SSTL_DESC_NULL;

  CHK((fp = fopen(filename, "w+")) != NULL);
  rewind(fp);

  CHK(sstl_load_binary(sstl, NULL) == RES_BAD_ARG);
  CHK(sstl_load_binary(NULL, filename) == RES_BAD_ARG);
  CHK(sstl_load_binary(sstl, "none.stl") == RES_IO_ERR);
  /* A binary cannot be empty */
  CHK(sstl_load_binary(sstl, filename) == RES_BAD_ARG);

  CHK(sstl_load_stream_binary(NULL, fp, filename) == RES_BAD_ARG);
  CHK(sstl_load_stream_binary(sstl, NULL, filename) == RES_BAD_ARG);
  CHK(sstl_load_stream_binary(sstl, fp, NULL) == RES_BAD_ARG);
  /* A binary cannot be empty */
  CHK(sstl_load_stream_binary(sstl, fp, filename) == RES_BAD_ARG);

  /* Write the minimum data required by a binary StL */
  CHK(fwrite(header, sizeof(header), 1, fp) == 1);
  CHK(fwrite(&ntris, sizeof(ntris), 1, fp) == 1);
  rewind(fp);

  CHK(sstl_load_binary(sstl, filename) == RES_OK);
  CHK(sstl_get_desc(sstl, &desc) == RES_OK);
  CHK(desc.type == SSTL_BINARY);
  CHK(!strcmp(desc.filename, filename));
  CHK(desc.solid_name == NULL);
  CHK(desc.vertices_count == 0);
  CHK(desc.triangles_count == 0);

  CHK(sstl_load_stream_binary(sstl, fp, filename) == RES_OK);
  CHK(sstl_get_desc(sstl, &desc) == RES_OK);
  CHK(desc.type == SSTL_BINARY);
  CHK(!strcmp(desc.filename, filename));
  CHK(desc.solid_name == NULL);
  CHK(desc.vertices_count == 0);
  CHK(desc.triangles_count == 0);

  CHK(fclose(fp) == 0);
}

static void
check_1_triangle(struct sstl* sstl)
{
  char header[80] = {0};
  const uint32_t ntris = 1;
  const float normal[3] = {0.f, -1.f, 0.f};
  const float verts[9] = {
    0.f, 0.f, 0.f,
    1.f, 0.f, 0.f,
    0.f, 0.f, 1.f
  };
  const uint16_t nattrs = 0;
  const char* filename = "1_triangle.stl";
  FILE* fp = NULL;
  float v[3] = {0,0,0};
  struct sstl_desc desc = SSTL_DESC_NULL;

  CHK(sstl);

  CHK((fp = fopen(filename, "w")) != NULL);
  CHK(fwrite(header, sizeof(header), 1, fp) == 1);
  CHK(fwrite(&ntris, sizeof(ntris), 1, fp) == 1);
  CHK(fwrite(normal, sizeof(normal), 1, fp) == 1);
  CHK(fwrite(verts, sizeof(verts), 1, fp) == 1);
  CHK(fwrite(&nattrs, sizeof(nattrs), 1, fp) == 1);
  CHK(fclose(fp) == 0);

  CHK(sstl_load(sstl, filename) == RES_OK);
  CHK(sstl_get_desc(sstl, &desc) == RES_OK);

  CHK(desc.type == SSTL_BINARY);
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
}

static void
check_1_triangle_no_normal(struct sstl* sstl)
{
  char header[80] = {0};
  const uint32_t ntris = 1;
  const float normal[3] = {0.f, 0.f, 0.f};
  const float verts[9] = {
    0.f, 0.f, 0.f,
   -1.f, 0.f, 0.f,
    0.f, 0.f,-1.f
  };
  const uint16_t nattrs = 0;
  const char* filename = "1_triangle_no_normal.stl";
  FILE* fp = NULL;
  float v[3] = {0,0,0};
  struct sstl_desc desc = SSTL_DESC_NULL;

  CHK((fp = fopen(filename, "w+")) != NULL);
  CHK(fwrite(header, sizeof(header), 1, fp) == 1);
  CHK(fwrite(&ntris, sizeof(ntris), 1, fp) == 1);
  CHK(fwrite(normal, sizeof(normal), 1, fp) == 1);
  CHK(fwrite(verts, sizeof(verts), 1, fp) == 1);
  CHK(fwrite(&nattrs, sizeof(nattrs), 1, fp) == 1);
  rewind(fp);

  CHK(sstl_load_stream(sstl, fp, filename) == RES_OK);

  CHK(sstl_get_desc(sstl, &desc) == RES_OK);
  CHK(desc.type == SSTL_BINARY);
  CHK(!strcmp(desc.filename, filename));
  CHK(desc.solid_name == NULL);
  CHK(desc.vertices_count == 3);
  CHK(desc.triangles_count == 1);
  CHK(desc.indices[0] == 0);
  CHK(desc.indices[1] == 1);
  CHK(desc.indices[2] == 2);
  CHK(f3_eq(desc.vertices + 0*3, f3(v, 0.f, 0.f, 0.f)) == 1);
  CHK(f3_eq(desc.vertices + 1*3, f3(v,-1.f, 0.f, 0.f)) == 1);
  CHK(f3_eq(desc.vertices + 2*3, f3(v, 0.f, 0.f,-1.f)) == 1);

  /* Normal is automatically calculated */
  CHK(f3_eq(desc.normals, f3(v, 0.f, -1.f, 0.f)) == 1);

  CHK(fclose(fp) == 0);
}

static void
check_invalid_file(struct sstl* sstl)
{
  const char header[80] = {0};
  const uint32_t ntris = 1;
  const float N[3] = {0,0,0};
  const float v0[3] = {0,0,0};
  const float v1[3] = {1,0,0};
  const float v2[3] = {0,0,1};
  const uint16_t nattrs = 0;
  FILE* fp = NULL;
  float v[3] = {0,0,0};
  struct sstl_desc desc = SSTL_DESC_NULL;

  /* First, check that the file should be OK if all the data has been correctly
   * written, to make sure that the tests really check what we expect, i.e. a
   * bad formatting and not a faulty data set */
  CHK((fp = tmpfile()) != NULL);
  CHK(fwrite(&header, sizeof(char), sizeof(header), fp) == sizeof(header));
  CHK(fwrite(&ntris, sizeof(ntris), 1, fp) == 1);
  CHK(fwrite(N, sizeof(N), 1, fp) == 1);
  CHK(fwrite(v0, sizeof(v0), 1, fp) == 1);
  CHK(fwrite(v1, sizeof(v1), 1, fp) == 1);
  CHK(fwrite(v2, sizeof(v2), 1, fp) == 1);
  CHK(fwrite(&nattrs, sizeof(nattrs), 1, fp) == 1);
  rewind(fp);
  CHK(sstl_load_stream(sstl, fp, "Valid StL") == RES_OK);
  CHK(fclose(fp) == 0);

  CHK(sstl_get_desc(sstl, &desc) == RES_OK);
  CHK(desc.type == SSTL_BINARY);
  CHK(!strcmp(desc.filename, "Valid StL"));
  CHK(desc.vertices_count == 3);
  CHK(desc.triangles_count == 1);
  CHK(desc.indices[0] == 0);
  CHK(desc.indices[1] == 1);
  CHK(desc.indices[2] == 2);
  CHK(f3_eq(desc.vertices + 0*3, f3(v, 0.f, 0.f, 0.f)) == 1);
  CHK(f3_eq(desc.vertices + 1*3, f3(v, 1.f, 0.f, 0.f)) == 1);
  CHK(f3_eq(desc.vertices + 2*3, f3(v, 0.f, 0.f, 1.f)) == 1);
  CHK(f3_eq(desc.normals, f3(v, 0.f, -1.f, 0.f)) == 1);

  /* Header is too small */
  CHK((fp = tmpfile()) != NULL);
  CHK(fwrite(&header, sizeof(char), sizeof(header)-1, fp) == sizeof(header)-1);
  CHK(fwrite(&ntris, sizeof(ntris), 1, fp) == 1);
  rewind(fp);
  CHK(sstl_load_stream(sstl, fp, "Invalid StL") == RES_BAD_ARG);
  CHK(fclose(fp) == 0);

  /* Triangle is missing */
  CHK((fp = tmpfile()) != NULL);
  CHK(fwrite(&header, sizeof(char), sizeof(header), fp) == sizeof(header));
  CHK(fwrite(&ntris, sizeof(ntris), 1, fp) == 1);
  rewind(fp);
  CHK(sstl_load_stream(sstl, fp, "Invalid StL") == RES_BAD_ARG);
  CHK(fclose(fp) == 0);

  /* Triangle normal is missing */
  CHK((fp = tmpfile()) != NULL);
  CHK(fwrite(&header, sizeof(char), sizeof(header), fp) == sizeof(header));
  CHK(fwrite(&ntris, sizeof(ntris), 1, fp) == 1);
  CHK(fwrite(v0, sizeof(v0), 1, fp) == 1);
  CHK(fwrite(v1, sizeof(v1), 1, fp) == 1);
  CHK(fwrite(v2, sizeof(v2), 1, fp) == 1);
  CHK(fwrite(&nattrs, sizeof(nattrs), 1, fp) == 1);
  rewind(fp);
  CHK(sstl_load_stream_binary(sstl, fp, "Invalid StL") == RES_BAD_ARG);
  CHK(fclose(fp) == 0);

  /* One vertex of the triangle is wrongly written */
  CHK((fp = tmpfile()) != NULL);
  CHK(fwrite(&header, sizeof(char), sizeof(header), fp) == sizeof(header));
  CHK(fwrite(&ntris, sizeof(ntris), 1, fp) == 1);
  CHK(fwrite(N, sizeof(N), 1, fp) == 1);
  CHK(fwrite(v0, sizeof(v0), 1, fp) == 1);
  CHK(fwrite(v1, sizeof(v1)-1/*One byte is missing*/, 1, fp) == 1);
  CHK(fwrite(v2, sizeof(v2), 1, fp) == 1);
  CHK(fwrite(&nattrs, sizeof(nattrs), 1, fp) == 1);
  rewind(fp);
  CHK(sstl_load_stream(sstl, fp, "Invalid StL") == RES_BAD_ARG);
  CHK(fclose(fp) == 0);

  /* The #attribs is missing */
  CHK((fp = tmpfile()) != NULL);
  CHK(fwrite(&header, sizeof(char), sizeof(header), fp) == sizeof(header));
  CHK(fwrite(&ntris, sizeof(ntris), 1, fp) == 1);
  CHK(fwrite(N, sizeof(N), 1, fp) == 1);
  CHK(fwrite(v0, sizeof(v0), 1, fp) == 1);
  CHK(fwrite(v1, sizeof(v1), 1, fp) == 1);
  CHK(fwrite(v2, sizeof(v2), 1, fp) == 1);
  rewind(fp);
  CHK(sstl_load_stream(sstl, fp, "Invalid StL") == RES_BAD_ARG);
  CHK(fclose(fp) == 0);
}

static void
check_tetrahedron(struct sstl* sstl)
{
  const char header[80] = {0};
  float v[3] = {0,0,0};
  const uint32_t ntris = 4;
  const uint16_t nattrs = 0;
  FILE* fp = NULL;
  struct sstl_desc desc = SSTL_DESC_NULL;

  CHK(sstl != NULL);

  CHK((fp = tmpfile()) != NULL);
  CHK(fwrite(header, sizeof(header), 1, fp) == 1);
  CHK(fwrite(&ntris, sizeof(ntris), 1, fp) == 1);

  CHK(fwrite(f3(v, 0.0f,-1.0f, 0.0f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(f3(v, 0.0f, 0.0f, 0.0f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(f3(v, 0.1f, 0.0f, 0.0f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(f3(v, 0.0f, 0.0f, 0.1f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(&nattrs, sizeof(nattrs), 1, fp) == 1);

  CHK(fwrite(f3(v, 0.0f, 0.0f,-1.f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(f3(v, 0.0f, 0.0f, 0.0f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(f3(v, 0.0f, 0.1f, 0.0f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(f3(v, 0.1f, 0.0f, 0.0f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(&nattrs, sizeof(nattrs), 1, fp) == 1);

  CHK(fwrite(f3(v,-1.0f, 0.0f, 0.0f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(f3(v, 0.0f, 0.0f, 0.0f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(f3(v, 0.0f, 0.0f, 0.1f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(f3(v, 0.0f, 0.1f, 0.0f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(&nattrs, sizeof(nattrs), 1, fp) == 1);

  CHK(fwrite(f3(v, 0.577f, 0.577f, 0.577f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(f3(v, 0.1f, 0.0f, 0.0f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(f3(v, 0.0f, 0.1f, 0.0f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(f3(v, 0.0f, 0.0f, 0.1f), sizeof(v), 1, fp) == 1);
  CHK(fwrite(&nattrs, sizeof(nattrs), 1, fp) == 1);

  rewind(fp);

  CHK(sstl_load_stream(sstl, fp, "Tetrahedron") == RES_OK);
  CHK(fclose(fp) == 0);

  CHK(sstl_get_desc(sstl, &desc) == RES_OK);
  CHK(desc.type == SSTL_BINARY);
  CHK(!strcmp(desc.filename, "Tetrahedron"));
  CHK(desc.solid_name == NULL);
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

#if defined(_WIN32)

#include <io.h>
#include <fcntl.h>
#include <process.h>
#include <windows.h>

/* ---------- 公共 child 调度封装 ---------- */

typedef void (*child_process_func)(void*);

/* 统一的 non-seekable pipe + writer thread 启动 */
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

    /* parent 永远关闭写端 */
    CHK(_close(fd[1]) == 0);

    if (out_thread)
        *out_thread = (HANDLE)th;
    else
        CloseHandle((HANDLE)th);
}

#endif /* _WIN32 */

struct binary_args {
    int write_fd;
};

static void
child_write_binary(void* arg)
{
    struct binary_args* a = arg;
    int fd = a->write_fd;
    float v[3] = { 0.f, 0.f, 0.f };

    const char header[80] = { 0 };
    const uint32_t ntris = 1;
    const uint16_t nattrs = 0;

    CHK(_write(fd, header, sizeof(header)) == sizeof(header));
    CHK(_write(fd, &ntris, sizeof(ntris)) == sizeof(ntris));
    CHK(_write(fd, f3(v, 0.f, -1.f, 0.f), sizeof(v)) == sizeof(v));
    CHK(_write(fd, f3(v, 1.f, 0.f, 0.f), sizeof(v)) == sizeof(v));
    CHK(_write(fd, f3(v, 0.f, 0.f, 1.f), sizeof(v)) == sizeof(v));
    CHK(_write(fd, f3(v, 0.f, 0.f, 0.f), sizeof(v)) == sizeof(v));
    CHK(_write(fd, &nattrs, sizeof(nattrs)) == sizeof(nattrs));

    CHK(_close(fd) == 0);
    free(a);
}

static void
check_no_seekable_file(struct sstl* sstl)
{
    int fd[2];
    FILE* fp = NULL;
    HANDLE th = NULL;
    struct sstl_desc desc = SSTL_DESC_NULL;
    float v[3];
    struct binary_args* args = NULL;

    CHK(_pipe(fd, 4096, _O_BINARY) == 0);

    args = malloc(sizeof(*args));
    CHK(args != NULL);

    args->write_fd = _dup(fd[1]);

    run_pipe_writer(fd, child_write_binary, args, &th);

    fp = _fdopen(fd[0], "rb");
    CHK(fp != NULL);

    CHK(sstl_load_stream(sstl, fp, "Piped StL") == RES_BAD_ARG);
    CHK(sstl_load_stream_binary(sstl, fp, "Piped StL") == RES_OK);
    CHK(fclose(fp) == 0);

    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);

    CHK(sstl_get_desc(sstl, &desc) == RES_OK);
    CHK(desc.type == SSTL_BINARY);
    CHK(!strcmp(desc.filename, "Piped StL"));
    CHK(desc.solid_name == NULL);
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

//static void
//check_no_seekable_file(struct sstl* sstl)
//{
//  float v[3] = {0,0,0};
//  int fd[2] = {0,0};
//  pid_t pid = 0;
//
//  CHK(pipe(fd) == 0);
//  CHK((pid = fork()) != -1);
//
//  if(pid == 0) {  /* Child process */
//    const char header[80] = {0};
//    const uint32_t ntris = 1;
//    const uint16_t nattrs = 0;
//
//    CHK(close(fd[0]) == 0); /* Close the unused input stream */
//    CHK(sstl_ref_put(sstl) == RES_OK); /* Release the unused sstl */
//
//    /* Write the binary StL */
//    CHK(write(fd[1], header, sizeof(header)) == sizeof(header));
//    CHK(write(fd[1], &ntris, sizeof(ntris)) == sizeof(ntris));
//    CHK(write(fd[1], f3(v, 0.f,-1.f, 0.f), sizeof(v)) == sizeof(v));
//    CHK(write(fd[1], f3(v, 1.f, 0.f, 0.f), sizeof(v)) == sizeof(v));
//    CHK(write(fd[1], f3(v, 0.f, 0.f, 1.f), sizeof(v)) == sizeof(v));
//    CHK(write(fd[1], f3(v, 0.f, 0.f, 0.f), sizeof(v)) == sizeof(v));
//    CHK(write(fd[1], &nattrs, sizeof(nattrs)) == sizeof(nattrs));
//
//    CHK(close(fd[1]) == 0);
//    exit(0);
//
//  } else { /* Parent process */
//    struct sstl_desc desc = SSTL_DESC_NULL;
//    FILE* fp = NULL;
//    CHK(close(fd[1]) == 0); /* Close the unused output stream */
//
//    CHK(fp = fdopen(fd[0], "r"));
//    CHK(sstl_load_stream(sstl, fp, "Piped StL") == RES_BAD_ARG);
//    CHK(sstl_load_stream_binary(sstl, fp, "Piped StL") == RES_OK);
//    CHK(fclose(fp) == 0);
//
//    CHK(sstl_get_desc(sstl, &desc) == RES_OK);
//    CHK(desc.type == SSTL_BINARY);
//    CHK(!strcmp(desc.filename, "Piped StL"));
//    CHK(desc.solid_name == NULL);
//    CHK(desc.vertices_count == 3);
//    CHK(desc.triangles_count == 1);
//    CHK(desc.indices[0] == 0);
//    CHK(desc.indices[1] == 1);
//    CHK(desc.indices[2] == 2);
//    CHK(f3_eq(desc.vertices + 0*3, f3(v, 1.f, 0.f, 0.f)) == 1);
//    CHK(f3_eq(desc.vertices + 1*3, f3(v, 0.f, 0.f, 1.f)) == 1);
//    CHK(f3_eq(desc.vertices + 2*3, f3(v, 0.f, 0.f, 0.f)) == 1);
//    CHK(f3_eq(desc.normals, f3(v, 0.f, -1.f, 0.f)) == 1);
//  }
//}


/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sstl* sstl = NULL;
  (void)argc, (void)argv;

  CHK(sstl_create(NULL, NULL, 3, &sstl) == RES_OK);

  check_api(sstl);
  check_1_triangle(sstl);
  check_1_triangle_no_normal(sstl);
  check_invalid_file(sstl);
  check_tetrahedron(sstl);
  check_no_seekable_file(sstl);

  CHK(sstl_ref_put(sstl) == RES_OK);
  CHK(mem_allocated_size() == 0);
  return 0;
}
#undef pid_t
