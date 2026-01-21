# Star Color Map

This library maps a real value to a color by interpolating for a given
value the colors predefined in a palette. A set of common palettes is
integrated into the library; see the scmap.h header for a complete list.

## Requirements

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.1

- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.

### Version 0.0.3

Fix compilation warnings detected by gcc 11

### Version 0.0.2

Sets the CMake minimum version to 3.1: since CMake 3.20, version 2.8 has
become obsolete.

### Version 0.0.1

Fix MSVC build.

## License

Copyright (C) 2020, 2021, 2023 |Méso|Star> (contact@meso-star.com)

Star-CMap is free software released under the GPL v3+ license: GNU GPL
version 3 or later. You are welcome to redistribute it under certain
conditions; refer to the COPYING file for details.
