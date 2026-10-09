# Star-Mesh

Star-Mesh loads surfacic or volumetric meshes saved in Star-Mesh file
format. See smsh.5 for details.

## Requirements

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys)
- [mandoc](https://mandoc.bsd.lv)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.2

- Add the smsh-desc utility.
  It prints the descriptor of a smsh file
- Add the smsh2vtk utility.
  It converts triangular or tetrahedral meshes saved in smshs format to
  VTK (legacy) format.
- Add the vtk-data utility.
  It formats a list of doubles as cell data in the legacy VTK format.
  It is then possible to attach data to a VTK mesh such as that produced
  by the smsh2vtk tool.
- Improves the building system.
  Simplify it by doing everything in one place (the Makefile).
  Add macros to control installation subdirectories.

### Version 0.1

- Make memory mapping optional.
  By default, data is now loaded into memory. Memory mapping becomes an
  option of the load functions, (forbidden on stdin).
- Write the man page directly in mdoc's roff macros, instead of using
  the intermediate scdoc source.
- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.

## License

Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)

Star-Mesh is free software released under the GPL v3+ license: GNU GPL
version 3 or later. You are welcome to redistribute it under certain
conditions; refer to the COPYING file for details.
