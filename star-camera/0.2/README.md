# Star Camera

This C library implements camera models such as pinhole, thin lens and
orthographic projection.

## Requirements

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.2

- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.

### Version 0.1

Add the `scam_perspective_get_solid_angle` function.

## License

Copyright (C) 2021-2023 |Méso|Star> (contact@meso-star.com)

Star-Cam is free software released under the GPL v3+ license: GNU GPL
version 3 or later. You are welcome to redistribute it under certain
conditions; refer to the COPYING file for details.
