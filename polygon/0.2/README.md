# Polygon

Polygon is a C library that triangulates polygons whose nonconsecutive
edges do not intersect. It implements a variation of the ear clipping
algorithm of Xianshu Kong et al. featured in [The Graham Scan
Triangulates Simple
Polygons](https://doi.org/10.1016/0167-8655\(90\)90089-K)

## Requirments

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys/)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.2

Replace CMake by POSIX make as build system

### Version 0.1.4

Fix warnings detected by gcc 11

### Version 0.1.3

Sets the CMake minimum version to 3.1: since CMake 3.20, version 2.8 has
become obsolete.

### Version 0.1.2

Update the version of the RSys dependency to 0.6: replace the deprecated
`[N]CHECK` macros by the new macro `CHK`.

## License

Copyright (C) 2014-2017, 2021-2023 Vincent Forest (vaplv@free.fr)

Polygon is free software released under GPL v3+ license: GNU GPL version
3 or later. You are welcome to redistribute it under certain conditions;
refer to the COPYING file for details.
