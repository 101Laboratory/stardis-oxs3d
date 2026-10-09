# Star STereo Lithography

Star-StL loads StL file format

## Requirements

- C compiler (C99)
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys/)
- [mandoc](https://mandoc.bsd.lv)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.7

- Add the Descriptors API to facilitate access to mesh data from its raw
  data structure.

### Version 0.6

- Improve code readability and simplicity by rewriting the internal
  architecture and implementation.
- Improve detection of the type of StL loaded (binary or ASCII).
- Prohibit the loading of untyped StL on non-searchable streams such as
  pipes, FIFOs or sockets.
  This would require duplicating input data, a complex implementation
  for a marginal feature, in other words, "it would suck".
- Rewrite the "write" API.
  It's designed to simplify streamed output, i.e. to write StLs whose
  data is not in memory.
- Make binary loading agnostic to the endianness of the host
  architecture.
- Added the sstl utility, which loads StL files and prints information
  about them.
- Improve the build system.
  Simplify it by doing everything in one place (the Makefile).
  Add macros to control installation sub-directories.

### Version 0.5.1

- Fix issues when reading on stdin
- Add a warning if trailing chars detected after the solid in ascii
  files.
- Add the read data type in the descriptor.
- Fix a false error log message (should have been an information
  message).

### Version 0.5

- Replace CMake by Makefile as build system.
- Provide a pkg-config file to link the library as an external
  dependency.

### Version 0.4.1

Correction of a compilation error highlighted in particular by GCC 4.9.2

### Version 0.4

- Add write functionality (either to files or streams),
- Add support for binary format,
- Fix tests (a test was run twice, the other one was not run).

### Version 0.3.4

- Sets the CMake minimum version to 3.1: since CMake 3.20, version 2.8
  has become obsolete.

### Version 0.3.3

- Fix star-stl lack of reentrancy. When used from a code using strtok,
  star-stl was corrupting the strtok context.

### Version 0.3.2

- Update the version of the RSys dependency to 0.6: replace the
  deprecated `[N]CHECK` macros by the new macro `CHK`.

## License

Copyright (C) 2015, 2016, 2019, 2021, 2023, 2025 |Méso|Star> (contact@meso-star.com)

Star-STL is free software released under GPL v3+ license: GNU GPL
version 3 or later. You are welcome to redistribute it under certain
conditions; refer to the COPYING file for details.

