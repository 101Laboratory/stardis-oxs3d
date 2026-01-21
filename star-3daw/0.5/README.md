# Star 3D shapes from Alias Wavefront geometries


Star-3DAW creates [Star-3D](https://gitlab.com/meso-star/star-3d) shapes
from polygonal geometries saved in the Alias Wavefront obj file format.

## Requirements

- C compiler
- POSIX make
- pkg-config
- [AW](https://gitlab.com/vaplv/loader_aw)
- [Polygon](https://gitlab.com/vaplv/polygon)
- [Star-3D](https://gitlab.com/meso-star/star-3d)
- [RSys](https://gitlab.com/vaplv/rsys)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.5

- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.

### Version 0.4.1

Sets the CMake minimum version to 3.1: since CMake 3.20, version 2.8 has
become obsolete.

### Version 0.4

Bump the version of the AW dependency to 2.0: update the code to handle
the API breaks introduce by this update.

## License

Copyright (C) 2015, 2016, 2020, 2021, 2023 |Méso|Star> (contact@meso-star.com)

Star-3DAW is free software released under GPL v3+ license: GNU GPL
version 3 or later. You are welcome to redistribute it under certain
conditions; refer to the COPYING file for details.
