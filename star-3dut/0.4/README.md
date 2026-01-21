# Star-3DUtilityToolkit

The Star-3DUT library generates the triangular mesh of 3D geometric
shapes. See the `s3dut.h` header file for the list of supported
geometries.

## Requirements

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys/)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.4

- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.

### Version 0.3.3

- Sets the CMake minimum version to 3.1: since CMake 3.20, version 2.8
  has become obsolete.

### Version 0.3.2

- Make the `s3dut.h` header compatible with C++.

### Version 0.3.1

- Update the version of the RSys dependency to 0.6: replace the
  deprecated `[N]CHECK` macros by the new macro `CHK`.

### Version 0.3

- Add the `s3dut_create_thin_cylinder` function that creates a
  triangulated cylinder. Both ends can be closed or left open.
- Add the `s3dut_create_thick_cylinder` function that creates a thick
  triangulated cylinder. Both ends can be closed or left open.
- Add the `s3dut_create_truncated_sphere` function that creates a
  triangulated UV sphere (possibly) truncated along the Z axis.
  Truncated ends can be closed or left open.
- Add the `s3dut_create_thick_truncated_sphere` function that creates a
  thick triangulated UV sphere (possibly) truncated along the Z axis.
  Truncated ends can be closed or left open.
- Add the `s3dut_create_super_shape` function that creates a
  triangulated super shape.
- Add the `s3dut_create_thick_truncated_super_shape` function that
  creates a thick triangulated super shape (possibly) truncated along
  the Z axis. Truncated ends can be closed or left open.
- Increase min number of slices for `s3dut_create_hemisphere` from 2 to
  3.

### Version 0.2

- Add the `s3dut_create_hemisphere` function that creates a triangulated
  UV hemisphere oriented along the positive Z axis.

## License

Copyright (C) 2016, 2017, 2020, 2021, 2023 |Méso|Star> (contact@meso-star.com)

Star-3DUT is free software released under GPL v3+ license: GNU GPL
version 3 or later. You are welcome to redistribute it under certain
conditions; refer to the COPYING file for details.
