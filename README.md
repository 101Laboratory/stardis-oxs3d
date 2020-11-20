# Stardis-Solver

Stardis-Solver is *free software* that solves *coupled* convecto - conducto -
radiative *thermal problems* in *complex* 2D and 3D *environments*. This C89
library internally relies on *Monte-Carlo* algorithms based on reformulations
of the main heat transfer phenomena as cross-recursive "thermal paths" that
explore space and time until a boundary condition or an initial condition is
found. The key concept here is that heat transfer phenomena are not considered
separately but naturally coupled via cross-recursive [Monte-Carlo
algorithms](https://hal.archives-ouvertes.fr/hal-02419604/).

The hypothesis these algorithms are based upon are the following:

- *conduction*: the discretization of thermal heat transfer in solids introduces
  the notion of a conductive path length within the Monte-Carlo algorithm.
  Solutions obtained using this algorithm are formally exact at the limit of a
  null path length. In practice, this path length has to be adapted for a given
  geometric configuration so that its value is small compared to the smallest
  typical length of a solid.
- *convection*: fluid media are supposed to be isothermal, even if their
  temperature may vary with time. This hypothesis relies on the assumption of
  perfectly agitated fluids.
- *radiation*: local radiative transfer is linearised, i.e. instead of writing
  the spectrally integrated net flux as a difference of temperatures to the
  power 4, it is assumed of the same form as the convective flux (as a
  difference of temperatures, multiplied by a radiative exchange coefficient).
  In order to be valid, this representation of radiative transfer exchanges
  requires that the temperature at any position and time is close to a known
  reference temperature.

In Stardis-Solver the system to simulate is represented by a *scene* whose
geometry defines the contour of the object only: in contrast to legacy thermal
solvers *no volumetric mesh* has to be provided. Each geometric primitive as an
associated *interface* that defines its physical properties (e.g. surface
emissivity) and reference the *media* defining the thermal properties on both
side of the primitive. The boundary and initial conditions are defined defined
on the interfaces (convection coefficient, fixed temperature/flux, etc.) and
the media (known temperature). Once fully and consistently described,
computations can be invoked on the resulting scene.

The main features of the solver are currently:

- *probe computation*: Stardis-Solver will compute the temperature at any given
  position (spatial and temporal). The main idea is that thermal paths start
  from this probe position, and scatter in space while going back in time,
  until a (spatial) boundary condition or a (temporal) initial condition is
  met. In addition to the value of temperature, using a Monte-Carlo method
  makes possible to compute a numerical uncertainty (standard deviation of the
  weight distribution) over each result.
- *flux computation*: Stardis-Solver can compute the flux over any surface (or
  group of surfaces) at any time. Alternatively, it can also compute the total
  energy output from a solid element where a internal source of power must be
  taken into account.
- *green function*: the value of temperature computed at a probe position is
  the average of the Monte-Carlo weight for every thermal path. In practice:
  when no internal power sources have to be considered, the weight of any given
  thermal path is the temperature of the boundary or initial condition that has
  been reached; when internal power sources or imposed fluxes are taken into
  account, additional contributions to the weight must be continuously
  evaluated by the thermal conduction algorithm, but these contributions are
  proportional to the local dissipated power/imposed flux.  In any case, the
  position and date at the end of each thermal path (and also accumulation
  coefficients) can be stored during a first complete Monte-Carlo simulation.
  This information, known as the Green function, can then be used in (very
  fast) post-processing to compute all required results for different boundary
  and initial conditions (and also different internal power sources/imposed
  flux). Note that when using the Green function, only boundary and initial
  conditions (as well as internal power sources) can be modified: in
  particular, the geometry, thermal properties and exchange coefficients have
  to remain identical.
- *path visualization*: Stardis-Solver can store the complete spatial and
  temporal position along a set of thermal paths, for latter visualization. In
  addition of their position and, each thermal path vertex register additional
  data as the type of thermal phenomena it simulates, the accumulated
  power/flux along the path, etc.

Stardis-Solver is currently used in two frameworks. The
[Stardis](https://gitlab.com/meso-star/stardis.git) CLI and its associated
tools is the reference workflow of Stardis-Solver. It proposes a complete
toolchain from fileformats describing the scene (geometry, thermal properties,
limit and boundary conditions) to computations and post-treatments of the
results ([Stardis-Green](https://gitlab.com/meso-star/stardis-green.git).
Stardis-Solver is also integrated into
[SYRTHES](https://www.edf.fr/en/the-edf-group/world-s-largest-power-company/activities/research-and-development/scientific-communities/simulation-softwares?logiciel=10818),
the general thermal free software developed by Electricité De France (EDF).

## How to build

Stardis-Solver is compatible GNU/Linux as well as Microsoft Windows 7 and
later, both in 64-bits. It was successfully built with the [GNU Compiler
Collection](https://gcc.gnu.org) (versions 4.9.2 and later) as well as with
Microsoft Visual Studio 2015.

It relies on the [CMake](http://www.cmake.org) and the
[RCMake](https://gitlab.com/vaplv/rcmake/) package to build.
It also depends on the
[RSys](https://gitlab.com/vaplv/rsys/),
[Star-2D](https://gitlab.com/meso-star/star-2d/),
[Star-3D](https://gitlab.com/meso-star/star-3d/),
[Star-Enclosures-3D](https://gitlab.com/meso-star/star-enclosures-3d/),
[Star-Enclosures-2D](https://gitlab.com/meso-star/star-enclosures-2d/) and
[Star-SP](https://gitlab.com/meso-star/star-sp/) libraries as well as on the
[OpenMP](http://www.openmp.org) 2.0 specification to parallelize its
computations.

First ensure that CMake and a C compiler that implements the OpenMP 2.0
specification are installed on your system. Then install the RCMake package as
well as all the aforementioned prerequisites. Finally generate the project from
the `cmake/CMakeLists.txt` file by appending to the `CMAKE_PREFIX_PATH`
variable the install directories of its dependencies.

## Release notes

### Version 0.11

- Add support of unsteady green evaluation. The resulting green function can
  then be used to quickly evaluate the system at the same time bit with
  different limit and initial conditions, volumetric powers and imposed fluxes.
- Add checks on green re-evaluation to ensure that the system remains unchanged
  regarding its scale factor and its reference temperature.
- Remove the ambient radiative temperature, the reference temperature and the
  geometry scale factor from the list of arguments submitted to the solve
  functions. They become scene arguments defined on scene creation.
- Update the `sdis_scene_[2d_]create` function profile: its data are now
  grouped into a variable of type `struct sdis_scene_create_args`.

### Version 0.10.1

- In green function estimation, the time sent to the user callbacks is no more
  the elapsed time from the beginning of the realisation: as in a regular
  computation, it is now the observation time.
- Fix the flux computation for boundaries with an imposed flux: it was
  previously ignored. The new `sdis_estimator_get_imposed_flux` function
  returns this estimated flux component.
- Return an error if the flux is computed at a boundary whose temperature is
  known: this configuration is not currently supported.
- Fix build with the CL compiler.

### Version 0.10

- Add support of green function [de]serialization. The
  `sdis_green_function_write` function serializes the green function into a
  stream while the `sdis_green_function_create_from_stream` function
  deserialize it. Note that the scene used to deserialize the green function
  must be the same of the one used to estimate it: the media and the interfaces
  have to be created in the same order, the scene geometry must be the same,
  etc.
- Add the `sdis_scene_find_closest_point` function: search the point onto the
  scene geometry that is the closest of the submitted position.
- Add the `sdis_compute_power` function that evaluates the power of a medium.
- Update the solver: the time of the sampled path is now rewind on solid
  reinjection.

### Version 0.9

- Update the API of the solve functions: the parameters of the simulation are
  now grouped into a unique data structure rather than separately submitted as
  function arguments. Thank to this structure and its default value, updating
  input parameters should now affect marginally the calling code.
- Improve the logger. Add a prefix to the printed text to indicate the type of
  the message (info, error or warning). Add a progress message during
  simulation.
- Bump the version of the Star-Enclosures <2D|3D> dependencies to 0.5

### Version 0.8.2

- Fix an issue when the `sdis_solve_boundary_flux` function was invoked on a
  boundary with radiative transfer: several sampled paths were rejected due to
  data inconsistencies.
- Fix a memory leak when the scene creation failed.
- Enable parallelism on Star-Enclosure[2D] to improve the performances of the
  enclosure extraction on the setup of the Stardis-Solver scene.

### Version 0.8.1

- Fix a solver issue that led to reject valid sampled paths.
- Bump the version of the Star-Enclosure[2D] libraries to 0.4.2. These versions
  fix a numerical issue that might led to an infinite loop at the scene creation.

### Version 0.8

- Drastically improve the robustness of the solver~: far less realisations are
  now rejected.
- Add the estimation of the time spent per realisation estimate. Add the
  `sdis_estimator_get_realisation_time` function that returns this estimate.
- Add the `sdis_estimator_buffer` API~: it manages a two dimensional array of
  regular estimators and provides global estimations over the whole estimators
  saved into the buffer.
- Update the signature of the `sdis_solve_camera` function~: it now returns a
  `sdis_estimator_buffer`. It now also supports time integration as well as
  heat paths registration.

### Version 0.7

#### Add Green function support

Provide new solve functions that compute and save the Green function, i.e. the
propagator used in regular solvers. The resulting Green function can be then
evaluated to obtain an estimate of the temperature.

The time spent to compute the Green function is comparable to the computation
time of regular solvers; actually, they rely on the same code. However, its
evaluation is instantaneous while it still handles the limit conditions, the
boundary fluxes and the power term of the media *at the moment* of the
evaluation. This means that one can estimate the Green function of a system
only one time and then evaluate it with different limit conditions, boundary
fluxes or power terms with negligible computation costs.

Currently, Stardis-Solver assumes that during the Green function estimation,
the properties of the system do not depend on time. In addition, it assumes
that the boundary fluxes and the volumetric powers are constants in time and
space.  Anyway, on Green function evaluation, the limit conditions of the
system can still vary in time and space; systems in steady state can be
simulated with Green functions.

#### Add heat path registration

Add the `int register_paths` mask to almost all solve functions to enable the
registration against the returned estimator of the failure and/or successful
random paths used by the solvers. For each path, the registered data are:

- the vertices of the path;
- the type of the path (failed or succeed);
- the type of the path vertices (conductive, convective or radiative);
- the Monte-Carlo weight of each path vertex;
- the current time of each path vertex.

Note that the amount of registered data can be huge if too more paths are
registered.  Consequently, this functionality should be used with few
realisations to obtain a subset of representative paths, or to only register
the few paths that failed in order to diagnose what went wrong.

#### Miscellaneous

- Add the `sdis_solve_medium` function: it  estimates the average temperature
  of a medium.
- Fix the setup of the interfaces: the interface associated to a geometric
  primitive could not be the right one.

### Version 0.6.1

- Bump version of the Star-Enclosures[2D] dependencies: the new versions fix
  issues in the construction of fluid enclosures.
- Bump version of the Star-<2D|3D> dependencies: the new versions rely on
  Embree3 rather than on Embree2 for their ray-tracing back-end.

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
  between the internal Stardis-Solver data and the user defined data was wrong.

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

First version and implementation of the Stardis-Solver API.

- Support fluid/solid and solid/solid interfaces.
- Only conduction is currently fully supported: convection and radiative
  temperature are not computed yet. Fluid media can be added to the system but
  currently, Stardis-Solver assumes that their temperature are known.

## License

Copyright (C) 2016-2020 |Meso|Star> (<contact@meso-star.com>). Stardis-Solver
is free software released under the GPLv3+ license: GNU GPL version 3 or later.
You are welcome to redistribute it under certain conditions; refer to the
COPYING files for details.

