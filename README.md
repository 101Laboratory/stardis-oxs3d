# Stardis

The purpose of this library is to solve coupled convecto - conducto - radiative
thermal problems in 2D and 3D environments.

## How to build

Stardis relies on the [CMake](http://www.cmake.org) and the
[RCMake](https://gitlab.com/vaplv/rcmake/) package to build.
It also depends on the
[RSys](https://gitlab.com/vaplv/rsys/),
[Star-2D](https://gitlab.com/meso-star/star-2d/),
[Star-3D](https://gitlab.com/meso-star/star-3d/),
[Star-Enclosures](https://gitlab.com/meso-star/star-enclosures/),
[Star-Enclosures2D](https://gitlab.com/meso-star/star-enclosures-2d/) and
[Star-SP](https://gitlab.com/meso-star/star-sp/) libraries as well as on the
[OpenMP](http://www.openmp.org) 2.0 specification to parallelize its
computations.

First ensure that CMake and a compiler that implements the OpenMP 1.2
specification are installed on your system. Then install the RCMake package as
well as all the aforementioned prerequisites. Finally generate the project from
the `cmake/CMakeLists.txt` file by appending to the `CMAKE_PREFIX_PATH`
variable the install directories of its dependencies.

## Release notes

### Version 0.6.1

- Bump version of the Star-Enclosures[2D] dependencies: the new versions fix
  issues in the construction of fluid enclosures.
- Bump version of the Star-<2D|3D> dependencies: the new versions rely on Embree3
  rather than on Embree2 for their ray-tracing back-end.

### Version 0.6

- Add the `sdis_solve_boundary` function: it computes the average temperature
  on a subset of geometric primitives.
- Add flux solvers: the new `sdis_solve_probe_boundary_flux` and
  `sdis_solve_boundary_flux` functions estimate the convective and radiative
  fluxes at a given surface position or for a sub-set of geometric primitives,
  respectively.
- Add support of time integration: almost all solvers can estimate the average
  temperature on a given time range. Only the `sdis_solve_camera` function does
  not support time integration, yet.
- Add support of an explicit initial time `t0` for the fluid.
- Fix a bug in the estimation of unknown fluid temperatures: the associativity
  between the internal Stardis data and the user defined data was wrong.

### Version 0.5

Add support of fluid enclosure with unknown uniform temperature.

- The convection coefficient of the surfaces surrounding a fluid whose
  temperature is unknown can vary in time and space. Anyway, the caller has to
  ensure that for each triangle of the fluid enclosure, the convection
  coefficient returned by its `struct sdis_interface_shader` - at a given
  position and time - is less than or equal to the `convection_coef_upper_bound`
  parameter of the shader.

### Version 0.4

Full rewrite of how the volumetric power is taken into account.

- Change the scheme of the random walk "solid re-injection": use a 2D
  re-injection scheme in order to handle 2D effects. On one hand, this scheme
  drastically improves the accuracy of the temperature estimation in solid with
  a volumetric power term. On the other hand it is more sensible to numerical
  imprecisions. The previous 1D scheme is thus used in situations where the 2D
  scheme exhibits too numerical issues, i.e. on sharp angles.
- Add the missing volumetric power term on solid re-injection.
- Add a corrective term to fix the bias on the volumetric power introduced when
  the random walk progresses at a distance of `delta` of a boundary.
- Add several volumetric power tests.
- Remove the `delta_boundary` parameter of the `struct sdis_solid_shader` data
  structure.

### Version 0.3

- Some interface properties become double sided: the temperature, emissivity
  and specular fraction is defined for each side of the interface. Actually,
  only the convection coefficient is shared by the 2 sides of the interface.
  The per side interface properties are grouped into the new `struct
  sdis_interface_side_shader` data structure.
- Add the support of fixed fluxes: the flux is a per side interface property.
  Currently, the flux is handled only for the interface sides facing a solid
  medium.
- Add the `sdis_scene_boundary_project_pos` function that computes the
  parametric coordinates of a world space position projected onto a given
  primitive with respect to its normal. If the projection lies outside the
  primitive, its parametric coordinates are wrapped against its boundaries in
  order to ensure that they are valid coordinates into the primitive. Actually,
  this function was mainly added to help in the definition of the probe
  position onto a boundary as expected by the
  `sdis_solve_probe_boundary` function.
- Update the default comportment of the interface shader when a function is not
  set.
- Rename the `SDIS_MEDIUM_<FLUID|SOLID>` constants in `SDIS_<FLUID|SOLID>`.
- Rename the `enum sdis_side_flag` enumerate in `enum sdis_side` and update its
  values.

### Version 0.2

- Add the support of volumic power to solid media: add the `volumic_power`
  functor to the `sdis_solid_shader` data structure that, once defined, should
  return the volumic power of the solid at a specific position and time. On
  solve invocation, the conductive random walks take into account this
  spatio-temporal volumic power in the computation of the solid temperature.
- Add the `sdis_solve_probe_boundary` function: it computes the temperature at
  a given position and time onto a geometric primitive. The probe position is
  defined by the index of the primitive and a parametric coordinates onto it.
- Add  the `sdis_scene_get_boundary_position` function: it computes a world
  space position from the index of a geometric primitive and a parametric
  coordinate onto it.
- Fix how the `sdis_solve_probe` was parallelised. The submitted `threads_hint`
  parameter was not correctly handled.

### Version 0.1

- Add the support of radiative temperature.
- Add the `sdis_camera` API: it defines a pinhole camera into the scene.
- Add the `sdis_accum_buffer` API: it is a pool of MC accumulators, i.e. a sum
  of MC weights and square weights.
- Add the `sdis_solve_camera` function: it relies on a `sdis_camera` and a
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

Stardis is Copyright (C) 2016-2019 |Meso|Star> (<contact@meso-star.com>). It is
free software released under the GPLv3+ license. You are welcome to
redistribute it under certain conditions; refer to the COPYING files for
details.

