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

#ifndef RSYS_H
#define RSYS_H

/* In C99 standard, C++ defines some macros (eg UINT32_MAX) only when
 * __STDC_LIMIT_MACROS or __STDC_CONSTANT_MACROS are defined before <stdint.h>
 * is included. The C11 standard removes this constraint. The following work
 * around always define those macros in C++ so that a C library implementation
 * which follows the C99 standard can be used in C++. */
#ifdef __cplusplus
  #ifndef __STDC_LIMIT_MACROS
    #define __STDC_LIMIT_MACROS
    #define STDC_LIMIT_MACROS_DEFINED_BY_RSYS__
  #endif
  #ifndef __STDC_CONSTANT_MACROS
    #define __STDC_CONSTANT_MACROS
    #define STDC_CONSTANT_MACROS_DEFINED_BY_RSYS__
  #endif
#endif

#if defined(_MSC_VER)
  #ifndef _CRT_SECURE_NO_WARNINGS
    #define _CRT_SECURE_NO_WARNINGS
  #endif
#endif

#include <stdint.h>

#ifdef STDC_LIMIT_MACROS_DEFINED_BY_RSYS__
  #undef __STDC_LIMIT_MACROS
  #undef STDC_LIMIT_MACROS_DEFINED_BY_RSYS__
#endif
#ifdef STDC_CONSTANT_MACROS_DEFINED_BY_RSYS__
  #undef __STDC_CONSTANT_MACROS
  #undef STDC_CONSTANT_MACROS_DEFINED_BY_RSYS__
#endif

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

/*******************************************************************************
 * Platform
 ******************************************************************************/
#if defined(_WIN32) || defined(WIN32)
#ifndef OS_WINDOWS
  #define OS_WINDOWS
#endif
#elif defined(__unix__) || defined(__unix) || defined(unix)
  #define OS_UNIX
#else
  #error "Unsupported OS"
#endif

/*******************************************************************************
 * Compiler
 ******************************************************************************/
#if defined(__GNUC__)
  #define COMPILER_GCC
#elif defined(_MSC_VER)
  #define COMPILER_MSVC
#else
  #error "Unsupported compiler"
#endif

/*******************************************************************************
 * Architecture
 ******************************************************************************/
#if defined(__x86_64__) || defined(_M_X64)
  #define ARCH_64BITS
#else
  #define ARCH_32BITS
#endif

/*******************************************************************************
 * Symbol visibility
 ******************************************************************************/
#if defined(COMPILER_GCC)
  #define EXPORT_SYM __attribute__((visibility("default")))
  #define IMPORT_SYM
  #define LOCAL_SYM __attribute__((visibility("hidden")))
#elif defined(COMPILER_MSVC)
  #define EXPORT_SYM __declspec(dllexport)
  #define IMPORT_SYM __declspec(dllimport)
  #define LOCAL_SYM
#else
  #error "Undefined symbol visibility macros"
#endif

#if defined(OS_WINDOWS)
  #define SHARED_LIBRARY_PREFIX ""
  #define SHARED_LIBRARY_SUFFIX ".dll"
#elif defined(OS_UNIX)
  #define SHARED_LIBRARY_PREFIX "lib"
  #define SHARED_LIBRARY_SUFFIX ".so"
#endif

#if !defined(SHARED_LIBRARY_PREFIX) || !defined(SHARED_LIBRARY_SUFFIX)
  #error "Undefined library suffix/prefix"
#endif

#define SHARED_LIBRARY_NAME(Lib) SHARED_LIBRARY_PREFIX Lib SHARED_LIBRARY_SUFFIX

#if defined(RSYS_SHARED_BUILD)
  #define RSYS_API extern EXPORT_SYM
#else
  #define RSYS_API extern IMPORT_SYM
#endif

/*******************************************************************************
 * Code inlining
 ******************************************************************************/
#if defined(COMPILER_GCC)
  #define FINLINE __inline__ __attribute__((always_inline))
  #define INLINE __inline__
  #define NOINLINE __attribute__((noinline))
#elif defined(COMPILER_MSVC)
  #define FINLINE __forceinline
  #define INLINE __inline
  #define NOINLINE __declspec(noinline)
#else
  #error "Undefined inlining macros"
#endif

/*******************************************************************************
 * Data alignment
 ******************************************************************************/
#if defined(COMPILER_GCC)
  #define ALIGN(Size) __attribute__((aligned(Size)))
  #define ALIGNOF(Type) __alignof__(Type)
#elif defined(COMPILER_MSVC)
  #define ALIGN(Size) __declspec(align(Size))
  #define ALIGNOF(Type) __alignof(Type)
#else
  #error "Undefined alignment macros"
#endif

#define ALIGN_SIZE(Size, Algnt) (((Size) + ((Algnt) - 1)) & ~((Algnt) - 1))
#define IS_ALIGNED(Addr, Algnt) (((uintptr_t)(Addr) & ((Algnt)-1)) == 0)

/*******************************************************************************
 * Atomic
 ******************************************************************************/
#if defined(COMPILER_GCC)
  #define ATOMIC int64_t
  #define ATOMIC_INCR(A) __sync_add_and_fetch((A), 1)
  #define ATOMIC_DECR(A) __sync_sub_and_fetch((A), 1)
  #define ATOMIC_ADD(A, V) __sync_add_and_fetch((A), V)
  #define ATOMIC_SUB(A, V) __sync_sub_and_fetch((A), V)
  #define ATOMIC_CAS(Atom, NewVal, Comparand) /* Return the initial value */   \
    __sync_val_compare_and_swap((Atom), (Comparand), (NewVal))
#elif defined(COMPILER_MSVC)
  #include <windows.h>
  #define ATOMIC int64_t
  #define ATOMIC_INCR(A) InterlockedIncrement64((LONG64*)(A))
  #define ATOMIC_DECR(A) InterlockedDecrement64((LONG64*)(A))
  #define ATOMIC_ADD(A, V) InterlockedAdd64((LONG64*)(A), (V))
  #define ATOMIC_SUB(A, V) InterlockedAdd64((LONG64*)(A), -(V))
  #define ATOMIC_CAS(Atom, NewVal, Comparand) /* Return the initial value */   \
    InterlockedCompareExchange64((LONG64*)(Atom), (NewVal), (Comparand))
#else
  #error "Undefined atomic operations"
#endif

/* Atomic set operation - sets value unconditionally, returns previous value */
#if defined(COMPILER_GCC)
  #define ATOMIC_SET(A, V) __sync_lock_test_and_set((A), (V))
#elif defined(COMPILER_MSVC)
  #define ATOMIC_SET(A, V) InterlockedExchange64((LONG64*)(A), (V))
#else
  #error "Undefined atomic set operation"
#endif

/* Atomic get operation - returns current value without modifying it */
#if defined(COMPILER_GCC)
  #define ATOMIC_GET(A) __sync_val_compare_and_swap((A), 0, 0)
#elif defined(COMPILER_MSVC)
  #define ATOMIC_GET(A) InterlockedCompareExchange64((LONG64*)(A), 0, 0)
#else
  #error "Undefined atomic get operation"
#endif

/*******************************************************************************
 * Function deprecation
 ******************************************************************************/
#if defined COMPILER_GCC
  #define DEPRECATED __attribute__((deprecated))
#elif defined COMPILER_MSVC
  #define DEPRECATED __declspec(deprecated)
#else
  #define DEPRECATED
#endif

/*******************************************************************************
 * Code checking
 ******************************************************************************/
#ifdef NDEBUG
  #define ASSERT(C) (void)0
#else
  #include <assert.h>
  #define ASSERT(C) assert(C)
#endif

#ifdef COMPILER_GCC
  #define STATIC_ASSERT(Cond, Msg)                                             \
    static char CONCAT(CONCAT(CONCAT(STATIC_ASSERT_, COUNTER), _), Msg)        \
      [1 -  2*(!(Cond))] __attribute__((unused))
#elif defined(COMPILER_MSVC)
  #define STATIC_ASSERT(Cond, Msg)                                             \
    static char CONCAT(CONCAT(CONCAT(STATIC_ASSERT_, COUNTER), _), Msg)        \
      [1 -  2*(!(Cond))]
#else
  #define STATIC_ASSERT(Cond, Msg) /* No static assert support */
#endif

#define VFATAL(Fmt, Args)                                                      \
  {                                                                            \
    fprintf(stderr, Fmt COMMA_##Args LIST_##Args);                             \
    ASSERT(0);                                                                 \
    abort();                                                                   \
  } (void)0

#define FATAL(Msg) VFATAL(Msg, ARG0())

#define CHK(Cond)                                                              \
  {                                                                            \
    if(!(Cond))                                                                \
      FATAL("error:" STR(__FILE__) ":" STR(__LINE__)"\n");                     \
  } (void)0

/*******************************************************************************
 * Branch prediction information
 ******************************************************************************/
#ifdef COMPILER_GCC
  #define LIKELY(X) __builtin_expect((X), 1)
  #define UNLIKELY(X) __builtin_expect((X), 0)
#elif defined(COMPILER_MSVC)
  #define LIKELY(X) (X)
  #define UNLIKELY(X) (X)
#else
  #define LIKELY(X) (X)
  #define UNLIKELY(X) (X)
#endif

/*******************************************************************************
 * Iteration
 ******************************************************************************/
/* Iterate over [Start, End) */
#define FOR_EACH(Id, Start, End)                                               \
  for((Id) = (Start); (Id) < (End); ++(Id))

/* Reverse iterrate over [Start, End) */
#define FOR_EACH_REVERSE(Id, Start, End)                                       \
  for((Id) = (Start); (Id) > (End); --(Id))

/*******************************************************************************
 * Instruction sets
 ******************************************************************************/
#if defined(COMPILER_GCC)
  #ifdef __SSE__
    #define SIMD_SSE
  #endif
  #ifdef __SSE2__
    #define SIMD_SSE2
  #endif
  #ifdef __SSE3__
    #define SIMD_SSE3
  #endif
  #ifdef __SSSE3__
    #define SIMD_SSSE3
  #endif
  #ifdef __SSE4_1__
    #define SIMD_SSE4_1
  #endif
  #ifdef __SSE4_2__
    #define SIMD_SSE4_2
  #endif
  #ifdef __AVX__
    #define SIMD_AVX
  #endif
  #ifdef __FMA__
    #define FMADD
  #endif
#endif

/*******************************************************************************
 * Variadic macros
 ******************************************************************************/
#define ARG0()
#define ARG1(A)
#define ARG2(A, B)
#define ARG3(A, B, C)
#define ARG4(A, B, C, D)
#define ARG5(A, B, C, D, E)
#define ARG6(A, B, C, D, E, F)
#define ARG7(A, B, C, D, E, F, G)
#define ARG8(A, B, C, D, E, F, G, H)
#define ARG9(A, B, C, D, E, F, G, H, I)

#define LIST_ARG0()
#define LIST_ARG1(A) A
#define LIST_ARG2(A, B) A, B
#define LIST_ARG3(A, B, C) A, B, C
#define LIST_ARG4(A, B, C, D) A, B, C, D
#define LIST_ARG5(A, B, C, D, E) A, B, C, D, E
#define LIST_ARG6(A, B, C, D, E, F) A, B, C, D, E, F
#define LIST_ARG7(A, B, C, D, E, F, G) A, B, C, D, E, F, G
#define LIST_ARG8(A, B, C, D, E, F, G, H) A, B, C, D, E, F, G, H
#define LIST_ARG9(A, B, C, D, E, F, G, H, I) A, B, C, D, E, F, G, H, I

#define COMMA_ARG0()
#define COMMA_ARG1(A) ,
#define COMMA_ARG2(A, B) ,
#define COMMA_ARG3(A, B, C) ,
#define COMMA_ARG4(A, B, C, D) ,
#define COMMA_ARG5(A, B, C, D, E) ,
#define COMMA_ARG6(A, B, C, D, E, F) ,
#define COMMA_ARG7(A, B, C, D, E, F, G) ,
#define COMMA_ARG8(A, B, C, D, E, F, G, H) ,
#define COMMA_ARG9(A, B, C, D, E, F, G, H, I) ,

/*******************************************************************************
 * Result constants
 ******************************************************************************/
typedef int res_T;
#define RES_OK 0
#define RES_BAD_ARG 1
#define RES_MEM_ERR 2
#define RES_IO_ERR 3
#define RES_UNKNOWN_ERR 4
#define RES_BAD_OP 5
#define RES_EOF 6 /* End Of File */

/*******************************************************************************
 * Miscellaneous
 ******************************************************************************/
#ifdef COMPILER_GCC
  /* On GCC compiler we follow the C-89 standard that does not support the
   * va_copy macro. We thus use the built-in __va_copy GCC extension */
  #define VA_COPY(VArgs, Args) __va_copy((VArgs), (Args))
#elif defined(COMPILER_MSVC)
  /* MSVC supports va_copy from C99 */
  #define VA_COPY(VArgs, Args) va_copy((VArgs), (Args))
#endif

#define BIT(Num) (1 << (Num))
#define BIT_I16(Num) (int16_t)((int16_t)1 << (Num))
#define BIT_I32(Num) (int32_t)((int32_t)1 << (Num))
#define BIT_I64(Num) (int64_t)((int64_t)1 << (Num))
#define BIT_U16(Num) (uint16_t)((uint16_t)1 << (Num))
#define BIT_U32(Num) (uint32_t)((uint32_t)1 << (Num))
#define BIT_U64(Num) (uint64_t)((uint64_t)1 << (Num))

#define CONCAT__(A, B) A ## B
#define CONCAT(A, B) CONCAT__(A, B)
#define SPLIT2(A) (A)[0], (A)[1]
#define SPLIT3(A) (A)[0], (A)[1], (A)[2]
#define SPLIT4(A) (A)[0], (A)[1], (A)[2], (A)[3]

#define CONTAINER_OF(Ptr, Type, Member) \
  ((Type*)((uintptr_t)Ptr - offsetof(Type, Member)))

#if defined COMPILER_GCC
  #define RESTRICT __restrict__
#elif defined COMPILER_MSVC
  #define RESTRICT __restrict
#else
  #define RESTRICT
#endif

#define COUNTER __COUNTER__

#define SWAP(Type, A, B)                                                       \
  {                                                                            \
    Type tmp__ = (A);                                                          \
    (A) = (B);                                                                 \
    (B) = tmp__;                                                               \
  } (void)0

#define STR__(X) #X
#define STR(X) STR__(X)

#define OFFSET_PTR(Ptr, Offset) (void*)((uintptr_t)(Ptr) + (Offset))

#ifdef COMPILER_GCC
  #define FUNC_NAME (__extension__ __FUNCTION__)
#else
  #define FUNC_NAME __FUNCTION__
#endif

#define MEM_AREA_OVERLAP(A, SzA, B, SzB)                                       \
  ((uintptr_t)(A) < (uintptr_t)(B)                                             \
   ? (uintptr_t)(B) < ((uintptr_t)(A) + (SzA))                                 \
   : (uintptr_t)(A) < ((uintptr_t)(B) + (SzB)))

#ifdef __cplusplus
  #define BEGIN_DECLS extern "C" {
  #define END_DECLS }
#else
  #define BEGIN_DECLS
  #define END_DECLS
#endif

#ifdef COMPILER_GCC
  #if __GNUC__ >= 7
    #define FALLTHROUGH __attribute__ ((fallthrough))
  #else
    #define FALLTHROUGH (void)0
  #endif
#else
  #define FALLTHROUGH (void)0
#endif

#endif /* RSYS_H */
