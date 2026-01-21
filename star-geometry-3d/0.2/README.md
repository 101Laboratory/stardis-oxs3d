# Star-geometry-3d

The purpose of this library is to help create clean and decorated 3D
geometries. These geometries are suitable to be partitioned into
enclosures using the star-enclosures-3d library. It also provides
mechanisms to construct triangle-related app data, detect
inconsistencies and dump the resulting geometry in various formats (OBJ,
VTK, C code chunks).

## Requirements

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys)
- [Star 3DUT](https://gitlab.com/meso-star/star-3dut) (optional)

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

### Version 0.1.3

- BugFix: when property conflicts where found, a wrong triangle was
  flaged.  As a consequence, geometry dumps of conflicting geometries
  where wrong.

### Version 0.1.2

- Fix OBJ dump.

### Version 0.1.1

- Fixed help headers failing to compile when included in C++ files.
- Fixed compilation warnings detected by GCC 11.

### Version 0.1

First version and implementation of the star-geometry-3d API.

- Creation of geometries in multiple steps, allowing advanced
  deduplication and application-data management
- Dump of geometries as OBJ or VTK files or as C code chunks

## License

Copyright © 2019, 2020, 2023, 2024
[|Méso|Star>](https://www.meso-star.com) (contact@meso-star.com)

It is free software released under the GPLv3+ license: GNU GPL version 3
or later. You are welcome to redistribute it under certain conditions;
refer to the COPYING files for details.
