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
  const char* filename = "test.stl";
  FILE* fp = NULL;
  struct sstl_desc desc = SSTL_DESC_NULL;

  CHK((fp = fopen(filename, "w+")) != NULL);
  rewind(fp);

  #define CHECK_EMPTY_FILE { \
    CHK(sstl_get_desc(sstl, &desc) == RES_OK); \
    CHK(!strcmp(desc.filename, filename)); \
    CHK(desc.type == SSTL_ASCII); \
    CHK(desc.solid_name == NULL); \
    CHK(desc.vertices_count == 0); \
    CHK(desc.triangles_count == 0); \
  } (void)0

  CHK(sstl_load(sstl, NULL) == RES_BAD_ARG);
  CHK(sstl_load(NULL, filename) == RES_BAD_ARG);
  CHK(sstl_load(sstl, "none.stl") == RES_IO_ERR);
  CHK(sstl_load(sstl, filename) == RES_OK); /* Empty file should be OK */

  CHK(sstl_get_desc(sstl, NULL) == RES_BAD_ARG);
  CHK(sstl_get_desc(NULL, &desc) == RES_BAD_ARG);
  CHECK_EMPTY_FILE;

  CHK(sstl_load_ascii(sstl, NULL) == RES_BAD_ARG);
  CHK(sstl_load_ascii(NULL, filename) == RES_BAD_ARG);
  CHK(sstl_load_ascii(sstl, "none.stl") == RES_IO_ERR);
  CHK(sstl_load_ascii(sstl, filename) == RES_OK); /* Empty file should be OK */
  CHECK_EMPTY_FILE;

  CHK(sstl_load_stream(NULL, fp, filename) == RES_BAD_ARG);
  CHK(sstl_load_stream(sstl, NULL, filename) == RES_BAD_ARG);
  CHK(sstl_load_stream(sstl, fp, NULL) == RES_BAD_ARG);
  CHK(sstl_load_stream(sstl, fp, filename) == RES_OK);
  CHECK_EMPTY_FILE;

  CHK(sstl_load_stream_ascii(NULL, fp, filename) == RES_BAD_ARG);
  CHK(sstl_load_stream_ascii(sstl, NULL, filename) == RES_BAD_ARG);
  CHK(sstl_load_stream_ascii(sstl, fp, NULL) == RES_BAD_ARG);
  CHK(sstl_load_stream_ascii(sstl, fp, filename) == RES_OK);
  CHECK_EMPTY_FILE;

  #undef CHECK_EMPTY_FILE

  CHK(fclose(fp) == 0);
}

static void
check_no_triangle(struct sstl* sstl)
{
  static const char* stl =
    "solid my_solid\n"
    "endsolid my_solid\n";
  const char* filename = "empty.stl";
  FILE* fp = NULL;
  struct sstl_desc desc = SSTL_DESC_NULL;

  CHK(sstl);

  CHK((fp = fopen(filename, "w+")) != NULL);
  CHK(fwrite(stl, sizeof(char), strlen(stl), fp) == strlen(stl));
  rewind(fp);

  CHK(sstl_load_stream(sstl, fp, filename) == RES_OK);
  CHK(sstl_get_desc(sstl, &desc) == RES_OK);
  CHK(desc.type == SSTL_ASCII);
  CHK(!strcmp(desc.filename, filename));
  CHK(!strcmp(desc.solid_name, "my_solid"));
  CHK(desc.vertices_count == 0);
  CHK(desc.triangles_count == 0);

  CHK(fclose(fp) == 0);
}

static void
check_1_triangle(struct sstl* sstl)
{
  static const char* stl =
    "solid\n"
    "  facet normal 0.0 -1.0 0.0\n"
    "    outer loop\n"
    "      vertex 0.0 0.0 0.0\n"
    "      vertex 1.0 0.0 0.0\n"
    "      vertex 0.0 0.0 1.0\n"
    "    endloop\n"
    "  endfacet\n"
    "endsolid";
  const char* filename = "1_triangle.stl";
  FILE* fp = NULL;
  float v[3] = {0,0,0};
  struct sstl_desc desc = SSTL_DESC_NULL;

  CHK(sstl);

  CHK((fp = fopen(filename, "w")) != NULL);
  CHK(fwrite(stl, sizeof(char), strlen(stl), fp) == strlen(stl));
  CHK(fclose(fp) == 0);

  CHK(sstl_load_ascii(sstl, filename) == RES_OK);
  CHK(sstl_get_desc(sstl, &desc) == RES_OK);

  CHK(desc.type == SSTL_ASCII);
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
check_1_triangle_with_noise(struct sstl* sstl)
{
  static const char* stl =
    "solid My Solid\n"
    "\n"
    "  facet normal 0.0 -1.0 0.0\n"
    "    outer loop     hophophophophop\n"
    "      vertex\t  0.0      0.0 0.0\n"
    "      vertex 1.0   0.0 0.0     \taaa\n"
    "      vertex 0.0   0.0     1.0\n"
    "    endloop   \n"
    "  endfacet   \t\t\t noise\n"
    "endsolid pouet\n";
  const char* filename = "1_triangle_with_noise.stl";
  FILE* fp = NULL;
  float v[3] = {0,0,0};
  struct sstl_desc desc = SSTL_DESC_NULL;

  CHK(sstl);

  CHK((fp = fopen(filename, "w")) != NULL);
  CHK(fwrite(stl, sizeof(char), strlen(stl), fp) == strlen(stl));
  CHK(fclose(fp) == 0);

  CHK(sstl_load(sstl, filename) == RES_OK);

  CHK(sstl_get_desc(sstl, &desc) == RES_OK);
  CHK(desc.type == SSTL_ASCII);
  CHK(!strcmp(desc.filename, filename));
  CHK(!strcmp(desc.solid_name, "My Solid"));
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
  static const char* stl =
    "solid\n"
    "  facet normal 0.0 0.0 0.0\n"
    "    outer loop\n"
    "      vertex 0.0 0.0 0.0\n"
    "      vertex 1.0 0.0 0.0\n"
    "      vertex 0.0 0.0 1.0\n"
    "    endloop\n"
    "  endfacet\n"
    "endsolid";
  const char* filename = "1_triangle_no_normal.stl";
  FILE* fp = NULL;
  float v[3] = {0,0,0};
  struct sstl_desc desc = SSTL_DESC_NULL;

  CHK((fp = fopen(filename, "w+")) != NULL);
  CHK(fwrite(stl, sizeof(char), strlen(stl), fp) == strlen(stl));
  rewind(fp);

  CHK(sstl_load_stream(sstl, fp, filename) == RES_OK);

  CHK(sstl_get_desc(sstl, &desc) == RES_OK);
  CHK(desc.type == SSTL_ASCII);
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

  /* Normal is automatically calculated */
  CHK(f3_eq(desc.normals, f3(v, 0.f, -1.f, 0.f)) == 1);

  CHK(fclose(fp) == 0);
}

static void
check_invalid_file(struct sstl* sstl)
{
  static const char* bad[] = {
    /* "endsolid" is missing */
    "solid\n"
    "  facet normal 0.0 -1.0 0.0\n"
    "    outer loop\n"
    "      vertex 0.0 0.0 0.0\n"
    "      vertex 1.0 0.0 0.0\n"
    "      vertex 0.0 0.0 1.0\n"
    "    endloop\n"
    "  endfacet\n",

    /* "solid" is missing */
    "  facet normal 0.0 -1.0 0.0\n"
    "    outer loop\n"
    "      vertex 0.0 0.0 0.0\n"
    "      vertex 1.0 0.0 0.0\n"
    "      vertex 0.0 0.0 1.0\n"
    "    endloop\n"
    "  endfacet\n"
    "endsolid\n",

    /* "normal" is missing */
    "solid\n"
    "  facet 0.0 -1.0 0.0\n"
    "    outer loop\n"
    "      vertex 0.0 0.0 0.0\n"
    "      vertex 1.0 0.0 0.0\n"
    "      vertex 0.0 0.0 1.0\n"
    "    endloop\n"
    "  endfacet\n"
    "endsolid\n",

    /* "facet" is missing */
    "solid\n"
    "  normal 0.0 -1.0 0.0\n"
    "    outer loop\n"
    "      vertex 0.0 0.0 0.0\n"
    "      vertex 1.0 0.0 0.0\n"
    "      vertex 0.0 0.0 1.0\n"
    "    endloop\n"
    "  endfacet\n"
    "endsolid\n",

    /* invalid vertex coordinate */
    "solid\n"
    "  facet normal 0.0 -1.0 0.0\n"
    "    outer loop\n"
    "      vertex 0.0 0.0 a.0\n"
    "      vertex 1.0 0.0 0.0\n"
    "      vertex 0.0 0.0 1.0\n"
    "    endloop\n"
    "  endfacet\n"
    "endsolid\n",

    /* Not enough vertices */
    "solid\n"
    "  facet normal 0.0 -1.0 0.0\n"
    "    outer loop\n"
    "      vertex 1.0 0.0 0.0\n"
    "      vertex 0.0 0.0 1.0\n"
    "    endloop\n"
    "  endfacet\n"
    "endsolid\n",

    /* "outer loop" is missing */
    "solid\n"
    "  facet normal 0.0 -1.0 0.0\n"
    "    vertex 0.0 0.0 0.0\n"
    "    vertex 1.0 0.0 0.0\n"
    "    vertex 0.0 0.0 1.0\n"
    "  endfacet\n"
    "endsolid\n"
  };
  const size_t nbads = sizeof(bad)/sizeof(const char*);
  FILE* fp = NULL;
  size_t i;

  CHK(sstl != NULL);

  FOR_EACH(i, 0, nbads) {
    CHK((fp = tmpfile()) != NULL);
    CHK(fwrite(bad[i], sizeof(char), strlen(bad[i]), fp) == strlen(bad[i]));
    rewind(fp);
    CHK(sstl_load_stream(sstl, fp, "invalid StL") == RES_BAD_ARG);
    CHK(fclose(fp) == 0);
  }
}

static void
check_tetrahedron(struct sstl* sstl)
{
  static const char* tetrahedron[] = {
    "solid cube corner\n",
    "  facet normal 0.0 -1.0 0.0\n",
    "    outer loop\n",
    "      vertex 0.0 0.0 0.0\n",
    "      vertex 0.1 0.0 0.0\n",
    "      vertex 0.0 0.0 0.1\n",
    "    endloop\n",
    "  endfacet\n",
    "  facet normal 0.0 0.0 -1.0\n",
    "    outer loop\n",
    "      vertex 0.0 0.0 0.0\n",
    "      vertex 0.0 0.1 0.0\n",
    "      vertex 0.1 0.0 0.0\n",
    "    endloop\n",
    "  endfacet\n",
    "  facet normal -1.0 0.0 0.0\n",
    "    outer loop\n",
    "      vertex 0.0 0.0 0.0\n",
    "      vertex 0.0 0.0 0.1\n",
    "      vertex 0.0 0.1 0.0\n",
    "    endloop\n",
    "  endfacet\n",
    "  facet normal 0.577 0.577 0.577\n",
    "    outer loop\n",
    "      vertex 0.1 0.0 0.0\n",
    "      vertex 0.0 0.1 0.0\n",
    "      vertex 0.0 0.0 0.1\n",
    "    endloop\n",
    "  endfacet\n",
    "endsolid\n"
  };
  FILE* fp = NULL;
  const char* filename = "Tetrahedron";
  const size_t nlines = sizeof(tetrahedron)/sizeof(const char*);
  struct sstl_desc desc = SSTL_DESC_NULL;
  float v[3];
  size_t i;

  CHK(sstl != NULL);

  CHK((fp = tmpfile()) != NULL);
  FOR_EACH(i, 0, nlines) {
    const size_t n = strlen(tetrahedron[i]);
    CHK(fwrite(tetrahedron[i], sizeof(char), n, fp) == n);
  }
  rewind(fp);

  CHK(sstl_load_stream_ascii(sstl, fp, filename) == RES_OK);
  CHK(fclose(fp) == 0);

  CHK(sstl_get_desc(sstl, &desc) == RES_OK);
  CHK(!strcmp(desc.filename, filename));
  CHK(!strcmp(desc.solid_name, "cube corner"));
  CHK(desc.type == SSTL_ASCII);
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

    /* 二进制 pipe，保证不发生 CRLF 转换 */
    //CHK(_pipe(fd, 4096, _O_BINARY) == 0);

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

struct ascii_args {
    int write_fd;
    const char* stl;
};

static void
child_write_ascii(void* arg)
{
    struct ascii_args* a = arg;
    size_t len = strlen(a->stl);

    CHK(_write(a->write_fd, a->stl, (int)len) == (int)len);
    CHK(_close(a->write_fd) == 0);
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
    struct ascii_args* args = NULL;

    const char* stl =
        "solid Triangle\n"
        "  facet normal 0.0 -1.0 0.0\n"
        "    outer loop\n"
        "      vertex 1.0 0.0 0.0\n"
        "      vertex 0.0 0.0 1.0\n"
        "      vertex 0.0 0.0 0.0\n"
        "    endloop\n"
        "  endfacet\n"
        "endsolid";

    CHK(_pipe(fd, 4096, _O_BINARY) == 0);

    args = malloc(sizeof(*args));
    CHK(args != NULL);

    args->write_fd = _dup(fd[1]);
    args->stl = stl;

    run_pipe_writer(fd, child_write_ascii, args, &th);

    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);

    fp = _fdopen(fd[0], "rb");
    CHK(fp != NULL);
    int res = sstl_load_stream(sstl, fp, "Piped StL");
    CHK(res == RES_BAD_ARG);
    CHK(sstl_load_stream_ascii(sstl, fp, "Piped StL") == RES_OK);
    CHK(fclose(fp) == 0);

    //WaitForSingleObject(th, INFINITE);
    //CloseHandle(th);

    CHK(sstl_get_desc(sstl, &desc) == RES_OK);
    CHK(desc.type == SSTL_ASCII);
    CHK(f3_eq(desc.normals, f3(v, 0.f, -1.f, 0.f)) == 1);
}

//static void
//check_no_seekable_file(struct sstl* sstl)
//{
//  const char* stl =
//    "solid Triangle\n"
//    "  facet normal 0.0 -1.0 0.0\n"
//    "    outer loop\n"
//    "      vertex 1.0 0.0 0.0\n"
//    "      vertex 0.0 0.0 1.0\n"
//    "      vertex 0.0 0.0 0.0\n"
//    "    endloop\n"
//    "  endfacet\n"
//    "endsolid";
//  int fd[2] = {0,0};
//  pid_t pid = 0;
//
//  CHK(pipe(fd) == 0);
//  CHK((pid = fork()) != -1);
//
//  if(pid == 0) {  /* Child process */
//    CHK(close(fd[0]) == 0); /* Close the unused input stream */
//    CHK(sstl_ref_put(sstl) == RES_OK); /* Release the unused sstl */
//
//    CHK(write(fd[1], stl, strlen(stl)) == (int)strlen(stl));
//    CHK(close(fd[1]) == 0);
//    exit(0);
//
//  } else { /* Parent process */
//    struct sstl_desc desc = SSTL_DESC_NULL;
//    float v[3];
//    FILE* fp = NULL;
//    const char* filename = "Piped StL";
//    CHK(close(fd[1]) == 0); /* Close the unused output stream */
//
//    CHK(fp = fdopen(fd[0], "r"));
//    CHK(sstl_load_stream(sstl, fp, filename) == RES_BAD_ARG);
//    CHK(sstl_load_stream_ascii(sstl, fp, filename) == RES_OK);
//    CHK(fclose(fp) == 0);
//
//    CHK(sstl_get_desc(sstl, &desc) == RES_OK);
//    CHK(desc.type == SSTL_ASCII);
//    CHK(!strcmp(desc.filename, filename));
//    CHK(!strcmp(desc.solid_name, "Triangle"));
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
  check_no_triangle(sstl);
  check_1_triangle(sstl);
  check_1_triangle_with_noise(sstl);
  check_1_triangle_no_normal(sstl);
  check_invalid_file(sstl);
  check_tetrahedron(sstl);
  check_no_seekable_file(sstl);

  CHK(sstl_ref_put(sstl) == RES_OK);
  CHK(mem_allocated_size() == 0);
  return 0;
}
#undef pid_t
