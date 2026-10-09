# RSys

RSys is a library written in C89 defining several basic components
useful for developing C programs but not implemented in the standard C
library. Among other things, it provides macros describing the host
environment (OS, compiler, processor architecture, etc.), low-level
abstractions (thread, timer, ref counter, etc.), generic containers
(dynamic arrays, table of hashes, linked lists etc.) or even basic
linear algebra to manipulate vectors and matrices of dimensions 2, 3 or
4.

## Requirements

- C compiler
- POSIX make

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.15

- Improve the building system. Simplify it by doing everything in one
  place (the Makefile) and building all the tests in the same way.
  Provide additional macros to control installation subdirectories.
- Remove (deprecated) support from Windows system and partial
  (deprecated) support from macOS. The library now targets portability
  of POSIX systems.
- Remove (deprecated) support from the CL compiler.
- Delete the code notified as obsolete for 8 years.
- Improve the portability of the endiannes functions by relying solely
  on the C language and thus being independent of the order of bytes of
  the machine, alignment, or anything else.
- Remove the OpenMP dependency. Only the tests used it. They now use
  POSIX threads and therefore depend only on the C compiler.

### Version 0.14

This version marks the replacement of CMake by Makefile as the build
system. The build procedure is written in POSIX make, which the user can
configure via the `config.mk` file. The POSIX script `make.sh` contains
commands that could be found directly in the Makefile, but which are
placed here to simplify its writing.

In addition to the features already provided by its CMake alternative,
the Makefile supports the construction of static libraries, provides an
uninstall target and updates compiler and linker flags to increase the
security and robustness of generated binaries. In any case, the main
motivation behind this writing is to use a good old well-established
standard with simple features, available on all UNIX systems, thus
simplifying its portability and support while being much lighter.

### Version 0.13

- Complete rewrite of sha256 calculations. To compute a hash following
  the sha256 cipher, we no longer rely on data pre-cut by the caller. As
  in GnuPG or the GNU C library, sha256 calculations now use a context
  that the caller updates with the data to digest. This rewrite
  introduces an API break.
- Add `size_to_cstr` function: it fills a C string with human readable
  text corresponding to a given size (in bytes). For example, 1024 can
  be formatted as `1 KB` or `1024 B`, depending on the input arguments.
- Fix `time_dump` function: correct invalid memory write as well as
  incorrect text formatting when write time is 0 and `TIME_DAY` flag is
  set.

### Version 0.12.1

- Fix compilation warnings with GCC 11.
- Fix the `text_reader` when the comment char is '\0'.
- Flush messages on default logger to force print them.
- License the project under the GPLv3+ license rather than the LGPLv3+
  license.

### Version 0.12

- Add the `cstr_parse_list` function that parses a string as a tuple.
  The function used to parse each element is defined by the caller.
- Add functions to encode/decode 2D and 3D morton indices.
- Add the `BIT_<U|I>16` macros that set a bit on a 16-bit integer.
- Add the FMADD macro defining wether the FMA instruction is supported
  by the COMPILer.
- Fix the `time_dump` function: the returned `real_dump_size` was
  wrongly evaluated.

### Version 0.11

- Add the `find_iterator` function to the hash table data structure.
  This function returns an iterator toward the hash table entry
  corresponding to the submitted key.
- Increase the capacity of the "variadic macros" up to 9 arguments.

### Version 0.10

- Add the `str_vprintf` and `str_append_[v]printf` functions to the
  string API that sets the string content or appends text to it,
  respectively. The input text is formatted following the `[v]printf`
  syntax, i.e. a literal is used to format the text and a variable list
  of data are either provided directly as function arguments (`printf`)
  or through a `va_list` (`vprintf`).
- Add macros and functions to deal with data endianness. The
  `BYTE_ORDER` macro defines the endianness of the host; its value can
  be `LITTLE_ENDIAN` or `BIG_ENDIAN`. The `byte_swap_<16|32|64>`
  functions revert the byte ordering of the submitted unsigned integer
  of 16, 32 or 64 bits.  Finally, the `<little|big>_endian_<16|32|64>`
  functions ensure that the input unsigned integer follows the little or
  big endian ordering: bytes are swapped only if the host endianness is
  not the expected one.
- Add support of 256 bits hash. The `hash_sha256` function digest input
  data wrt to the SHA-256 cryptographic hash algorithm. The
  `hash256_to_cstr` function converts a 256 bits hash to its
  corresponding lower case string.  Finally The `hash256_eq` function
  check that the submitted 256 bits hashes are the same.

### Version 0.9.1

- Add the `VFATAL` macro that works as the regular `FATAL` macro but
  with an additional variadic argument allowing to format the displayed
  error message as in `printf`.
- Add the `str_printf` function to the string API: it sets the string
  content with the data formatted wrt the `printf` syntax.

### Version 0.9

- Add the text reader API. A text reader reads lines from a text stream.
  In contrast to the `getline` POSIX function, a text reader skips the
  empty lines as well as comments that are text directly following a
  user defined comment character.
- Make silent the library API: the library functions do not print
  anymore any message on the standard error.

### Version 0.8.1

- Fix the allocation policy of the dynamic array that exhibited strong
  performance issues when the `resize` function was used in a
  `push_back` manner, i.e. to allocate a new entry at the end of the
  dynamic array.

### Version 0.8

- Update the allocation policy of the dynamic arrays: the `reserve` and
  `resize` functions strictly allocate the submitted size if there is no
  sufficient space already allocated.
- Add the `DARRAY_BUF` macro: give a direct access to the internal
  buffer of the dynamic array. It is a less verbose alternative to the
  `data_get` and `cdata_get` dynamic array functions that operate on
  strongly typed data structures.
- Add the `d22_rotation` and `f22_rotation` functions: compute a 2x2
  rotation matrix in the XY plane.

### Version 0.7.1

- Add the FALLTHROUGH macro that disables compilation warning on switch
  statement fallthrough.
- Fix a possible wrong return code in the set routine of the hash
  tables.

### Version 0.7

- Add the `res_to_cstr` function that returns a string describing the
  submitted result code.
- Add the `SIMD_AVX` macro that is defined if the AVX instruction set is
  available on the host machine.
- Fix the aligned allocation of the LIFO allocator: the returned address
  was not necessarily aligned on the expected value.
- Fix the `search_lower_bound` algorithm.
- Fix a compilation error when RSys was linked against a version of the
  GNU C Library less than 2.17.

### Version 0.6.1

- Fix the detection of a 64-bits architecture on the CL compiler.

### Version 0.6

- Remove the `big_buffer` container. Its implementation was awful and it
  was thus useless.
- Add the read/write mutex API and provide an implementation with POSIX
  threads.
- Add the `CHK` macro. It replaces the `[N]CHECK` macros that become
  deprecated.

### Version 0.5

- Add the `big_buffer` container, i.e. out of core dynamic array of POD
  data.
- Update the `clock_time` API: the `time_<add|current|sub>` functions
  return a pointer toward the result.
- Add the `time_zero` function that cleans-up the submitted time.
- Add a Last In First Out (LIFO) allocator. It uses a pre-allocated
  memory pool to store a stack of allocated memory blocks. A memory
  block is allocated on top of the stack. On "free" invocation, it is
  marked as freed but it is effectively removed from the allocated
  memory when it lies on top of the stack.

### Version 0.4

- Add the `double2`, `double3`, `double4`, `double33`, `double22` and
  `double44` data types that provide the same functionalities of their
  `float` alternative.
- Add the `purge` function to the hash table and the dynamic array data
  structures. This function not only resets the state of the structure,
  as the `clear` function, but also frees its internal memory.
- Implement a new image API that provides and explicit image data
  structure.  The old API is still available but is deprecated.

## License

Copyright © 2013-2023, 2025 Vincent Forest (vaplv@free.fr)

RSys is free software released under the GPL v3+ license: GNU GPL
version 3 or later. You are welcome to redistribute it under certain
conditions; refer to the COPYING files for details.
