# Stardis-Solver

Stardis-Solver is *free software* that solves *coupled* convecto -
conducto - radiative *thermal problems* in *complex* 2D and 3D
*environments*. This C89 library internally relies on *Monte-Carlo*
algorithms based on reformulations of the main heat transfer phenomena
as cross-recursive "thermal paths" that explore space and time until a
boundary condition or an initial condition is found. The key concept
here is that heat transfer phenomena are not considered separately but
naturally coupled via cross-recursive [Monte-Carlo
algorithms](https://doi.org/10.1371/journal.pone.0283681)

In Stardis-Solver the system to simulate is represented by a *scene*
whose geometry defines the contour of the object only: in contrast to
legacy thermal solvers *no volumetric mesh* has to be provided. Each
geometric primitive as an associated *interface* that defines its
physical properties (e.g. surface emissivity) and reference the *media*
defining the thermal properties on both side of the primitive.  The
boundary conditions are defined on the interfaces (convection
coefficient, fixed temperature/flux, etc.), the media (known
temperature), the *radiative environment* (ambient radiative
temperature) or an *external source* (incident flux).  Once fully and
consistently described, computations can be invoked on the resulting
scene.

Stardis-Solver is currently used in two frameworks. The
[Stardis](https://gitlab.com/meso-star/stardis.git) CLI and its
associated tools is the reference workflow of Stardis-Solver. It
proposes a complete toolchain from fileformats describing the scene
(geometry, thermal properties, limit and boundary conditions) to
computations and post-treatments of the results
([Stardis-Green](https://gitlab.com/meso-star/stardis-green.git)).
Stardis-Solver is also integrated into
[SYRTHES](https://www.edf.fr/en/the-edf-group/world-s-largest-power-company/activities/research-and-development/scientific-communities/simulation-softwares?logiciel=10818),
the general thermal free software developed by Electricité De France
(EDF).

The hypothesis these algorithms are based upon are the following:

- *Conduction*: Stardis-Solver offers two ways of sampling unsteady
  Brownian motion to solve for conductivity in a solid.  The delta
  sphere algorithm is based on the discrimination of thermal heat
  transfer in solids, which introduces the notion of conductive path
  length. Solutions obtained using this algorithm are formally exact
  to the limit of zero path length. In practice, this path length
  must be adapted to a given geometrical configuration so that its
  value is small compared to the smallest typical space-time length
  of a solid.

  As an alternative, the
  [Walk on Sphere](https://www.jstor.org/stable/2237369)
  algorithm samples an unbiased diffuse trajectory in a solid with
  Dirichlet boundary conditions, unbiased with respect to what numerical
  accuracy can account for. Its coupling with other boundary or
  connection conditions behaves as with the delta sphere algorithm, i.e.
  the solution is exact when the length of the trajectory used as a
  first-order approximation tends towards 0.

- *Convection*: fluid media are supposed to be isothermal, even if
  their temperature may vary with time. This hypothesis relies on
  the assumption of perfectly agitated fluids.

- *Radiation*: local radiative transfer is solved by an [iterative
  numerical method](https://hal.archives-ouvertes.fr/tel-03266863/)
  (Picard algorithm) that requires the knowledge of a reference
  temperature field.  At the basic level (one level of recursion), and
  using a uniform reference temperature field, this algorithm translates
  into the hypothesis of a linearised radiative transfer. Using a higher
  order or recursion makes possible to converge the result closer to the
  solution of a rigorous spectrally-integrated radiative transfer (a
  difference of temperatures to the power 4 when integrated over the
  whole spectrum).  The higher the recursion order, the better will be
  the convergence of the algorithm.

Despite its specific advantages, Stardis-Solver is not meant to fully
replace already well established and highly validated thermal simulation
tools.  Instead, it can be seen as an additional tool that can be useful
for various purposes:

- *Probe computation*: Stardis-Solver will *not* compute the full
  temperature field of a system; instead, it can be used to focus on a
  specific spatial/temporal zone of the system.  The main idea is that
  thermal paths start from this probe position, and scatter in space
  while going back in time, until a (spatial) boundary condition or a
  (temporal) initial condition is met. In addition to the value of
  temperature, using a Monte-Carlo method makes possible to compute a
  numerical uncertainty (standard deviation of the weight distribution)
  over each result.

- *Integrated calculation*: thanks to Monte-Carlo, Stardis-Solver can
  calculate the temperature of an entire volume or surface, over a given
  time range, without increasing calculation time or uncertainty
  compared with a probe-based calculation at a specific point in time.

- *Flux computation*: Stardis-Solver can compute the flux over any
  surface (or group of surfaces) at any time or time range.
  Alternatively, it can also compute the total energy output from a
  solid element where a internal source of power must be taken into
  account.

- *Infrared rendering*: Stardis-Solver can be used to simulate the
  radiative temperature reaching the sensor of a camera.
  The [image rendering](https://dx.doi.org/10.1145/3592121)
  will take into account coupled (unsteady) phenomena and the entire
  geometry *without* ever having to calculate the temperature field.

- *Green function*: the value of temperature computed at a probe
  position is the average of the Monte-Carlo weight for every thermal
  path. In practice: when no internal power sources have to be
  considered, the weight of any given thermal path is the temperature of
  the boundary or initial condition that has been reached; when internal
  power sources, imposed fluxes or an external source are taken into
  account, additional contributions to the weight must be continuously
  evaluated by the thermal conduction algorithm, but, under linear
  assumption, these contributions are proportional to the local
  dissipated power, imposed flux or external source radiance.

  So the position and date at the end of each thermal path (and also
  accumulation coefficients) can be stored during a first complete
  Monte-Carlo simulation. This is actually an estimates of the Green
  function, can then be used in (very fast)
  [post-processing](https://doi.org/10.1016/j.cpc.2023.108911) to
  compute all required results for different boundary and initial
  conditions (and also different internal power density, imposed or
  incident flux). Note that when using the Green function, only boundary
  and initial conditions (as well as internal power sources) can be
  modified: in particular, the geometry, thermal properties and exchange
  coefficients have to remain identical. Furthermore, the green function
  is only valid under the assumption of linearised radiative transfer.

- *Path visualization*: Stardis-Solver can store the complete spatial
  and temporal position along a set of thermal paths, for latter
  visualization. In addition of their position and, each thermal path
  vertex register additional data as the type of thermal phenomena it
  simulates, the accumulated power/flux along the path, etc.

## Requirements

- C compiler with OpenMP support
- POSIX make
- pkg-config
- Message Passing Interface (optional)
- [RSys](https://gitlab.com/vaplv/rsys)
- [Star 2D](https://gitlab.com/meso-star/star-2d)
- [Star 3D](https://gitlab.com/meso-star/star-3d)
- [Star 3DUT](https://gitlab.com/meso-star/star-3dut)
  (optional for tests)
- [Star Enclosures 2D](https://gitlab.com/meso-star/star-enclosures-2D)
- [Star Enclosures 3D](https://gitlab.com/meso-star/star-enclosures-3D)
- [Star SamPling](https://gitlab.com/meso-star/star-sp)
- [Star WoS Functions](https://gitlab.com/meso-star/star-wf)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.16.2

- Continue to improve numerical robustness when sampling the radiative
  path, both in long waves and when processing an external source.
- Update the attenuation of numerical inaccuracies when the candidate
  path for reinjection is located approximately on one of the vertices
  of a triangle.

### Version 0.16.1

- Corrected net flux calculation on surfaces with several Robin boundary
  conditions. Depending on the configuration, such a calculation could
  be impossible and return an error.
- Improve the performance of external flux calculations by avoiding the
  need to calculate the contribution of external flux for surfaces with
  zero emissivity.
- Mitigate numerical errors when sampling radiative paths.

### Version 0.16

#### Add support for custom sampling of solid paths

Add the `sample_path` functor as a member variable of the
`sdis_solid_shader` data structure which, once defined, is called to
sample a trajectory in this solid, instead of relying on the internal
sampling procedures for unsteady conductive paths (i.e. walk on
delta sphere or walk on sphere).

Coupling takes place as usual, i.e. at the boundary. Only path sampling
is delegated to the caller. In other words, the connection physics
remain the same, and the user only has control over the physical model
of the custom solid.

Note that to help the user map the sampled path to the solver limits
(i.e. the geometry reached by the path), we added the `sdis_primkey` API
which constructs a unique key from the vertices of a segment/triangle.
We can then use this key as input to the new functions
`sdis_scene_get_s<2|3>d_primitive` to find the solver primitive which
corresponds to this segment/triangle. It is therefore sufficient to know
the *exact* coordinates of the boundary mesh to geometrically couple the
paths sampled by the user with the boundaries of the geometries managed
by the solver.

#### Correcting the sampling of radiative paths

Allows sampling of radiative paths in enclosures with multiple media.
Multiple media are often used to define a set of Robin boundary
conditions. And although sampling a convective path in such an enclosure
is an error (since it is outside the space of convective paths), it is
still possible to sample radiative paths in this enclosure (it is
perfectly defined in the space of radiative paths).

For example, it is now possible to render an infrared image of a system
with a set of Robin boundary conditions defined from a set of fluids
with fixed temperatures.

#### Bug fixes

- Fixes the `sdis_solve_probe_boundary_list` function. The calculation
  did not use the expected number of threads, which could lead to an
  invalid memory access.
- Updated error handling when resolving temperatures for several probes.
  The calculation of a probe no longer stops as soon as a realisation is
  rejected. As with the calculation of a single probe, if a result is
  rejected, it is simply not taken into account in the estimate.
- Corrects numerical problems when sampling a conductive path with WoS.
  The thresholds used to detect/manage numerical problems were
  calculated from absolute distances in metres. This method was not
  numerically robust when these distances were very small. They
  are now calculated in relation to the delta of the solid, which must
  be able to take account of spatio-temporal temperature gradients while
  being large enough to allow numerical estimation.
- Corrects the position of the path when the initial condition is
  reached during the sampling of a conductive path with WoS. The unit
  used to update the position was wrong when the `fp_to_meter`
  variable was not set to 1. In addition, the position at which the
  initial condition was reached could be outside the solid.

### Version 0.15.2

Correction of pkg-config file. A missing private dependency could lead
to link editing errors when the user statically links to the library.

### Version 0.15.1

Make the radiative environment time-dependent, so that it can vary not
only with respect to the direction along path that reaches it, but also
as a function of time at which it is reached.

### Version 0.15

#### New conduction algorithm

Addition of a new algorithm for sampling a potentially unsteady
conductive path based on the Walk on Sphere (WoS) algorithm. Currently,
unlike the previous delta sphere algorithm, the initial condition must
be constant for the solid. Power density is also supported, but unlike
the delta sphere algorithm, it too cannot vary in time and space. In all
cases, the two algorithms coexist and can be selected via a new input
parameter to the resolution functions. By default, the delta sphere
algorithm is used.

Note that the WoS algorithm is considered unbiased when sampling a
diffuse trajectory in a solid with Dirichlet boundary conditions. Even
if a numerical parameter is used to stop the algorithm when the
trajectory is close to the boundary, this distance can be set to the
machine's accuracy without significant impact on performance. Hence its
unbiased nature in relation to numerical precision. Its coupling with
other boundary or connection conditions behaves as with the delta sphere
algorithm, i.e. the solution tends towards the exact solution when the
delta tends towards 0.

#### External spherical source

An external spherical source can be added to the scene. Once defined, it
is considered as a new boundary condition whose contribution is
calculated at the solid/fluid interfaces in the form of an external net
flux. A new interface parameter controls on which solid/fluid interfaces
this external net flux is imposed. By default, when an external source
is defined, its contribution is calculated on all solid/fluid
interfaces. We point out that the radiative properties of the surface
can now vary according to the radiative source, which can now be
internal or external.

The net external flux is calculated by sampling the radiative paths to
evaluate the direct and diffuse part of the incident flux due to the
external source. We emphasize that Stardis-Solver does not manage
semi-traparent media, but that the external source provides the optional
`diffuse_radiance` parameter which, once defined, corresponds to the
radiance emitted by the external source and diffused at least once into
the environment. In this way, the diffuse part of the flux manages not
only the radiation from the external source that reaches the interface
after one or more reflections, but also the external radiation scattered
in the environment, here simply represented by the `diffuse_radiance`
parameter.

The external spherical source is defined by its position, radius, power
and diffuse radiance (see above). While the radius is constant, the
position and power are time-dependent, and the diffuse radiance also
depends on the direction along which the sampled trajectory reaches the
environment.

The external spherical source is fully supported when estimating the
green function. Only the positions of the spherical source must remain
the same between green function estimation and use.

Finally, as with net imposed fluxes and power densities, this external
source term cannot be used when solving non-linear radiative exchanges
using Picard iterations.

#### Allow relative temperatures

Allow to perform calculations relative to a given temperature T. In this
case, the temperatures managed by Stardis would be relative to T and
could therefore be negative, since they would express a deviation from
T. It should be noted that reference temperatures must always be
positive, i.e. expressed in the absolute domain. Finally, we emphasize
that relative calculations only make sense in linear situations, i.e.
negative temperatures are not valid for systems with non-linear
radiative exchanges.

This is a major break in the API that callers *must* take into account.
Until now, negative temperatures were considered as unknown
temperatures, whereas they are now valid. For example, an interface with
a negative temperature could be considered adiabatic, whereas it is now
a Dirichlet boundary condition. In other words, the same data could
define totally different systems before or after this version.

The macro `SDIS_TEMPERATURE_NONE` is added to define the unknown
temperature value. The two helper macros `SDIS_TEMPERATURE_IS_KNOWN` and
`SDIS_TEMPERATURE_IS_UNKNOWN`  are also provided to test whether the
temperature is known or not.

#### Parallelize multiple probe resolutions

Add `sdis_solve_probe_list` and `sdis_solve_probe_boundary_list`
functions. Unlike their single-probe counterpart, these functions
parallelize the list of probes, rather than parallelizing Monte Carlo
realizations. Calling these functions is therefore more advantageous in
terms of load distribution when the number of probes to be evaluated is
large compared to the cost of calculating a single probe.

#### Miscellaneous

- Updated the function profile used to define surface radiative
  properties, i.e. surface emissivity and specular fraction. These can
  now vary according to the source identifier (internal or external).
- Make the radiative environment programmable. From now on, its API is
  the same as that of other resources such as interfaces or media. Its
  temperature and reference temperature are retrieved by functions whose
  input argument is the direction of the radiative path.
- Add an optional user filter function as input argument to
  `sdis_scene_find_closest_point` function. The user can set their own
  filter function to manage the candidate points to be closest. This
  gives the caller a fine control during the inquiry to access the
  geometry and traversal of the accelerating structure.
- CMake has been replaced by Makefile as the build system, and a
  pkg-config file is provided to link the library as an external
  dependency. Compiler and linker flags have also been updated to
  increase the security and robustness of generated binaries.

### Version 0.14

- The net flux imposed can be combined with other boundary/connection
  conditions, i.e. a net flux can be set in addition to convective
  exchange and radiative transfer.
- Added support for plain text log messages. Until now, log messages
  were intended to be read by a VT100-like terminal and could therefore
  contain escape sequences that required post-processing to store them
  in plain text log files.
- Added support for user-defined signature on the green function. It
  allows to check that, when reloaded, a green function is the one
  expected by the user according to its own constraints that the green
  function cannot check itself such as, for example, that the same
  deltas are used in conductive random walks.
- Changes the value of the constant `SDIS_VOLUMIC_POWER_NONE`. Its
  previous value of zero caused problems during the evaluation of the
  propagator: a media with a power density of zero was not registered in
  the list of media with a volumic power. A volumic power that was not
  zero was therefore not taken into account during the re-evaluation of
  the propagator. The constant is now set to `DBL_MAX`, which means that
  the medium has no power density, while a value of 0 is now treated as
  any valid power density term.

### Version 0.13.1

Fixed compilation errors and compilation warnings displayed on some
versions of GCC.

### Version 0.13

#### Non linear radiative transfer

Uses a new [iterative numerical
method](https://hal.archives-ouvertes.fr/tel-03266863/) to estimate
radiative transfer. With a recursion level of 1, this is equivalent to a
linearization of the radiative transfer but with a reference temperature
that can vary in time and space. By using a higher-order recursion, one
can converge towards a rigorous estimate that takes into account the
non-linearity of the radiative transfer; the higher the recursion order,
the better the convergence, but with the counterpart of an increase in
calculation time.

#### Distributed memory parallelism

Uses message passing interface to distribute computation across multiple
computers. Stardis-Solver now, uses a mixed parallelism: on one computer
(i.e.  a node), it uses a shared memory parallelism and relies on the
message passing interface to parallelize calculations between several
nodes.

#### Type and state of the random number generator

Adds the member input variable `rng_type` to the solve functions. It
defines the type of random number generator to use when no generator is
defined. Note that the `sdis_solve_camera` function does not have a
random number generator as an input variable and has therefore been
updated to support it.

#### Reading the source code

Refactoring and deep rewriting of the source code to simplify its
reading.

### Version 0.12.3

Fix green paths ending in a fluid (transcient computation): The path's
end was not correctly registred and the path was later treated as
failed.

### Version 0.12.2

- Sets the required version of Star-SampPling to 0.12. This version
  fixes compilation errors with gcc 11 but introduces API breaks.
- Fix warnings detected by gcc 11.

### Version 0.12.1

Updates the way numerical issues are handled during a conductive random
walk.  Previously, a zealous test would report a numerical error and
stop the calculations when that error could be handled.

### Version 0.12

Add the support of thermal contact resistance between two solids: the
new `thermal_contact_resistance` functor on the data structure `struct
sdis_interface_shader` defines the thermal resistance contact in
K.m^2.W^-1 at a given time and at a specific position onto the
interface.

### Version 0.11

- Add support of unsteady green evaluation. The resulting green function
  can then be used to quickly evaluate the system at the same time but
  with different limit and initial conditions, volumetric powers and
  imposed fluxes.
- Add checks on green re-evaluation to ensure that the system remains
  unchanged regarding its scale factor and its reference temperature.
- Remove the ambient radiative temperature, the reference temperature
  and the geometry scale factor from the list of arguments submitted to
  the solve functions. They become scene arguments defined on scene
  creation.
- Update the `sdis_scene_[2d_]create` function profile: its data are now
  grouped into a variable of type `struct sdis_scene_create_args`.

### Version 0.10.1

- In green function estimation, the time sent to the user callbacks is
  no more the elapsed time from the beginning of the realisation: as in
  a regular computation, it is now the observation time.
- Fix the flux computation for boundaries with an imposed flux: it was
  previously ignored. The new `sdis_estimator_get_imposed_flux` function
  returns this estimated flux component.
- Return an error if the flux is computed at a boundary whose
  temperature is known: this configuration is not currently supported.
- Fix build with the CL compiler.

### Version 0.10

- Add support of green function [de]serialization. The
  `sdis_green_function_write` function serializes the green function
  into a stream while the `sdis_green_function_create_from_stream`
  function deserialize it. Note that the scene used to deserialize the
  green function must be the same of the one used to estimate it: the
  media and the interfaces have to be created in the same order, the
  scene geometry must be the same, etc.
- Add the `sdis_scene_find_closest_point` function: search the point
  onto the scene geometry that is the closest of the submitted position.
- Add the `sdis_compute_power` function that evaluates the power of a
  medium.
- Update the solver: the time of the sampled path is now rewind on solid
  reinjection.

### Version 0.9

- Update the API of the solve functions: the parameters of the
  simulation are now grouped into a unique data structure rather than
  separately submitted as function arguments. Thank to this structure
  and its default value, updating input parameters should now affect
  marginally the calling code.
- Improve the logger. Add a prefix to the printed text to indicate the
  type of the message (info, error or warning). Add a progress message
  during simulation.
- Bump the version of the Star-Enclosures <2D|3D> dependencies to 0.5

### Version 0.8.2

- Fix an issue when the `sdis_solve_boundary_flux` function was invoked
  on a boundary with radiative transfer: several sampled paths were
  rejected due to data inconsistencies.
- Fix a memory leak when the scene creation failed.
- Enable parallelism on Star-Enclosure[2D] to improve the performances
  of the enclosure extraction on the setup of the Stardis-Solver scene.

### Version 0.8.1

- Fix a solver issue that led to reject valid sampled paths.
- Bump the version of the Star-Enclosure[2D] libraries to 0.4.2. These
  versions fix a numerical issue that might led to an infinite loop at
  the scene creation.

### Version 0.8

- Drastically improve the robustness of the solver~: far less
  realisations are now rejected.
- Add the estimation of the time spent per realisation estimate. Add the
  `sdis_estimator_get_realisation_time` function that returns this
  estimate.
- Add the `sdis_estimator_buffer` API~: it manages a two dimensional
  array of regular estimators and provides global estimations over the
  whole estimators saved into the buffer.
- Update the signature of the `sdis_solve_camera` function~: it now
  returns a `sdis_estimator_buffer`. It now also supports time
  integration as well as heat paths registration.

### Version 0.7

#### Add Green function support

Provide new solve functions that compute and save the Green function,
i.e. the propagator used in regular solvers. The resulting Green
function can be then evaluated to obtain an estimate of the temperature.

The time spent to compute the Green function is comparable to the
computation time of regular solvers; actually, they rely on the same
code. However, its evaluation is instantaneous while it still handles
the limit conditions, the boundary fluxes and the power term of the
media *at the moment* of the evaluation. This means that one can
estimate the Green function of a system only one time and then evaluate
it with different limit conditions, boundary fluxes or power terms with
negligible computation costs.

Currently, Stardis-Solver assumes that during the Green function
estimation, the properties of the system do not depend on time. In
addition, it assumes that the boundary fluxes and the volumetric powers
are constants in time and space.  Anyway, on Green function evaluation,
the limit conditions of the system can still vary in time and space;
systems in steady state can be simulated with Green functions.

#### Add heat path registration

Add the `int register_paths` mask to almost all solve functions to
enable the registration against the returned estimator of the failure
and/or successful random paths used by the solvers. For each path, the
registered data are:

- the vertices of the path;
- the type of the path (failed or succeed);
- the type of the path vertices (conductive, convective or radiative);
- the Monte-Carlo weight of each path vertex;
- the current time of each path vertex.

Note that the amount of registered data can be huge if too more paths
are registered.  Consequently, this functionality should be used with
few realisations to obtain a subset of representative paths, or to only
register the few paths that failed in order to diagnose what went wrong.

#### Miscellaneous

- Add the `sdis_solve_medium` function: it  estimates the average
  temperature of a medium.
- Fix the setup of the interfaces: the interface associated to a
  geometric primitive could not be the right one.

### Version 0.6.1

- Bump version of the Star-Enclosures[2D] dependencies: the new versions
  fix issues in the construction of fluid enclosures.
- Bump version of the Star-<2D|3D> dependencies: the new versions rely
  on Embree3 rather than on Embree2 for their ray-tracing back-end.

### Version 0.6

- Add the `sdis_solve_boundary` function: it computes the average
  temperature on a subset of geometric primitives.
- Add flux solvers: the new `sdis_solve_probe_boundary_flux` and
  `sdis_solve_boundary_flux` functions estimate the convective and
  radiative fluxes at a given surface position or for a sub-set of
  geometric primitives, respectively.
- Add support of time integration: almost all solvers can estimate the
  average temperature on a given time range. Only the
  `sdis_solve_camera` function does not support time integration, yet.
- Add support of an explicit initial time `t0` for the fluid.
- Fix a bug in the estimation of unknown fluid temperatures: the
  associativity between the internal Stardis-Solver data and the user
  defined data was wrong.

### Version 0.5

Add support of fluid enclosure with unknown uniform temperature.

- The convection coefficient of the surfaces surrounding a fluid whose
  temperature is unknown can vary in time and space. Anyway, the caller
  has to ensure that for each triangle of the fluid enclosure, the
  convection coefficient returned by its
  `struct sdis_interface_shader` - at a given position and time - is
  less than or equal to the `convection_coef_upper_bound` parameter of
  the shader.

### Version 0.4

Full rewrite of how the volumetric power is taken into account.

- Change the scheme of the random walk "solid re-injection": use a 2D
  re-injection scheme in order to handle 2D effects. On one hand, this
  scheme drastically improves the accuracy of the temperature estimation
  in solid with a volumetric power term. On the other hand it is more
  sensible to numerical imprecisions. The previous 1D scheme is thus
  used in situations where the 2D scheme exhibits too numerical issues,
  i.e. on sharp angles.
- Add the missing volumetric power term on solid re-injection.
- Add a corrective term to fix the bias on the volumetric power
  introduced when the random walk progresses at a distance of `delta` of
  a boundary.
- Add several volumetric power tests.
- Remove the `delta_boundary` parameter of the `struct
  sdis_solid_shader` data structure.

### Version 0.3

- Some interface properties become double sided: the temperature,
  emissivity and specular fraction is defined for each side of the
  interface. Actually, only the convection coefficient is shared by the
  2 sides of the interface.  The per side interface properties are
  grouped into the new `struct sdis_interface_side_shader` data
  structure.
- Add the support of fixed fluxes: the flux is a per side interface
  property.  Currently, the flux is handled only for the interface sides
  facing a solid medium.
- Add the `sdis_scene_boundary_project_pos` function that computes the
  parametric coordinates of a world space position projected onto a
  given primitive with respect to its normal. If the projection lies
  outside the primitive, its parametric coordinates are wrapped against
  its boundaries in order to ensure that they are valid coordinates into
  the primitive. Actually, this function was mainly added to help in the
  definition of the probe position onto a boundary as expected by the
  `sdis_solve_probe_boundary` function.
- Update the default comportment of the interface shader when a function
  is not set.
- Rename the `SDIS_MEDIUM_<FLUID|SOLID>` constants in
  `SDIS_<FLUID|SOLID>`.
- Rename the `enum sdis_side_flag` enumerate in `enum sdis_side` and
  update its values.

### Version 0.2

- Add the support of volumic power to solid media: add the
  `volumic_power` functor to the `sdis_solid_shader` data structure
  that, once defined, should return the volumic power of the solid at a
  specific position and time. On solve invocation, the conductive random
  walks take into account this spatio-temporal volumic power in the
  computation of the solid temperature.
- Add the `sdis_solve_probe_boundary` function: it computes the
  temperature at a given position and time onto a geometric primitive.
  The probe position is defined by the index of the primitive and a
  parametric coordinates onto it.
- Add  the `sdis_scene_get_boundary_position` function: it computes a
  world space position from the index of a geometric primitive and a
  parametric coordinate onto it.
- Fix how the `sdis_solve_probe` was parallelised. The submitted
  `threads_hint` parameter was not correctly handled.

### Version 0.1

- Add the support of radiative temperature.
- Add the `sdis_camera` API: it defines a pinhole camera into the scene.
- Add the `sdis_accum_buffer` API: it is a pool of MC accumulators, i.e.
  a sum of MC weights and square weights.
- Add the `sdis_solve_camera` function: it relies on a `sdis_camera` and
  a `sdis_accum_buffer` to compute the radiative temperature that
  reaches each pixel of an image whose definition is defined by the
  caller. Note that actually this function uses the same underlying MC
  algorithm behind the `sdis_solve_probe` function.

### Version 0.0

First version and implementation of the Stardis-Solver API.

- Support fluid/solid and solid/solid interfaces.
- Only conduction is currently fully supported: convection and radiative
  temperature are not computed yet. Fluid media can be added to the
  system but currently, Stardis-Solver assumes that their temperature
  are known.

## License

Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)

Stardis-Solver is free software released under the GPLv3+ license: GNU
GPL version 3 or later.  You are welcome to redistribute it under
certain conditions; refer to the COPYING files for details.
