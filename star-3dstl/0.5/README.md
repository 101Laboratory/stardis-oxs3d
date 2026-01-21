# Star 3D shapes from STereo Lithography geometries

Star-3DSTL creates Star-3D shapes from triangulated geometries saved in
STL file format.

## Requirements

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys)
- [Star-3D](https://gitlab.com/meso-star/star-3d)
- [Star-STL](https://gitlab.com/meso-star/star-stl)

## How to build

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.5

- Fix API breaks with Star-StL 0.6
- Improve the build system.
  Simplify it by doing everthing in one place (the Makefile).
  Add macros to control installation sub-directories.

### Version 0.4

- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.

### Version 0.3.2

Sets the CMake minimum version to 3.1: since CMake 3.20, version 2.8 has
become obsolete.

## License

Copyright (C) 2016, 2018, 2021, 2023, 2025 |Méso|Star> (contact@meso-star.com)

Star-3DSTL is free software released under GPL v3+ license: GNU GPL
version 3 or later. You are welcome to redistribute it under certain
conditions; refer to the COPYING file for details.
