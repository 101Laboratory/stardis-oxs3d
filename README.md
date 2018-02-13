# Stardis

The purpose of this library is to solve coupled convecto - conducto - radiative
thermal problems.

## How to build

Stardis relies on the [CMake](http://www.cmake.org) and the
[RCMake](https://gitlab.com/vaplv/rcmake/) package to build.
It also depends on the
[RSys](https://gitlab.com/vaplv/rsys/),
[Star-2D](https://gitlab.com/meso-star/star-2d/),
[Star-3D](https://gitlab.com/meso-star/star-3d/) and
[Star-SP](https://gitlab.com/meso-star/star-sp/) libraries as well as on the
[OpenMP](http://www.openmp.org) 1.2 specification to parallelize its
computations.

First ensure that CMake and a compiler that implements the OpenMP 1.2
specification are installed on your system. Then install the RCMake package as
well as all the aforementioned prerequisites. Finally generate the project from
the `cmake/CMakeLists.txt` file by appending to the `CMAKE_PREFIX_PATH`
variable the install directories of its dependencies.

## Release notes

### Version 0.0

First version and implementation of the Stardis solver API.

- Support fluid/solid and solid/solid interfaces.
- Only conduction is currently fully supported: convection and radiative
  temperature are not computed yet. Fluid media can be added to the system but
  currently, Stardis assumes that their temperature are known.

## License

Stardis is Copyright (C) |Meso|Star> 2016-2018 (<contact@meso-star.com>). It is
free software released under the GPLv3+ license. You are welcome to
redistribute it under certain conditions; refer to the COPYING files for
details.

