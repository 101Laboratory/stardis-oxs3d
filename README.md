# Stardis

The purpose of this library is to solve coupled convecto - conducto - radiative
thermal problems in 2D and 3D environments.

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

### Version 0.1

- Add the support of radiative temperature.
- Add the `sdis_camera` API : it defines a pinhole camera into the scene.
- Add the `sdis_accum_buffer` API : it is a pool of MC accumulators, i.e. a sum
  of MC weights and square weights.
- Add the `sdis_solve_camera` function : it relies on a `sdis_camera` and a
  `sdis_accum_buffer` to compute the radiative temperature that reaches each
  pixel of an image whose definition is defined by the caller. Note that
  actually this function uses the same underlying MC algorithm behind the
  `sdis_solve_probe` function.

### Version 0.0

First version and implementation of the Stardis solver API.

- Support fluid/solid and solid/solid interfaces.
- Only conduction is currently fully supported: convection and radiative
  temperature are not computed yet. Fluid media can be added to the system but
  currently, Stardis assumes that their temperature are known.

## License

Stardis is Copyright (C) 2016-2018 |Meso|Star> (<contact@meso-star.com>). It is
free software released under the GPLv3+ license. You are welcome to
redistribute it under certain conditions; refer to the COPYING files for
details.

