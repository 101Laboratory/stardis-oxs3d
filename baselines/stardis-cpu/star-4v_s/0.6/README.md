# Star-4V/S

This program statistically computes "An invariant property of diffuse
random walks" (S. Blanco and R. Fournier - Europhysics Letters 2003) on
an user defined geometry. The submitted geometry must define a set of
polygonal meshes saved with respect to the Alias Wavefront Obj
fileformat. The resulting surfaces must define a set of closed volumes
whose normals point inward the volumes. The program assumes that the
shape surfaces are fully emissive and the diffuse property of their
volume is constant.

## Requirements

- C compiler
- POSIX make
- pkg-config
- mandoc
- RSys
- Star-3D
- Star-3DAW
- Star-SamPling
- Star-MonteCarlo

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.6

- Update dependencies and correct API breaks.
- Replace CMake with POSIX make as build system.
- Update command line syntax to respect POSIX utility conventions.
- Add a manual page.

### Version 0.5

Make the code compatible with Star-MC 0.5.

### Version 0.4

Make the code compatible with Star-3D 0.8.

### Version 0.3

Make the code compatible with Star-MC 0.4.

### Version 0.2

Make the code compatible with Star-3D 0.4 and Star-SP 0.4.

## License

Copyright (C) 2015-2018, 2021, 2024 |Méso|Star> (contact@meso-star.com)

Star-4V/S is free software released under the GPL v3+ license: GNU GPL
version 3 or later. You are welcome to redistribute it under certain
conditions; refer to the COPYING file for details.
