# stardis

## Overview

Stardis is a software dedicated to the resolution of coupled
convective-conductive-radiative thermal problems in 3D environments.
It is based on
[Stardis Solver](https://gitlab.com/meso-star/stardis-solver) and
exposes some of the main features of the solver in an easy to use way.
Using Stardis is a practical way of carrying out thermal studies on CAD
models which can be exported from Salomé or other similar software.

## Requirements

- C compiler
- POSIX make
- pkg-config
- Message Passing Interface (optional)
- [mandoc](https://mandoc.bsd.lv)
- [RSys](https://gitlab.com/vaplv/rsys)
- [Star 3D](https://gitlab.com/meso-star/star-3d)
- [Stardis Solver](https://gitlab.com/meso-star/stardis-solver)
- [Star Enclosure 3D](https://gitlab.com/meso-star/star-enclosures-3d)
- [Star Geometry 3D](https://gitlab.com/meso-star/star-geometry-3d)
- [Star SamPling](https://gitlab.com/meso-star/star-sp)
- [Star StL](https://gitlab.com/meso-star/star-stl)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.12

- Add a new solid/fluid connection with imposed flux.
  The new directives `F_SOLID_FLUID_CONNECTION` and
  `F_SOLID_FLUID_CONNECTION_PROG` describe a connection between a solid
  and a fluid with imposed flux at their common interface, the latter
  using programmable properties.
- Add the `-f` and `-l` options to calculate the flux density for a
  probe or a list of probes, respectively.
- Add the missing inclusion of the `limits.h` header to the programmable
  properties file.
  This is necessary in order to use the `UINT_MAX` constant.
- Partial correction of the calculation of a system's temperature range.
  Only the temperature ranges of the connection conditions and boundary
  conditions were used.
  Now, programmable solids are also taken into account.
  Regular solids and fluids should also be taken into account.
  However, this more significant correction requires further review and
  additional testing to ensure that the new method for calculating the
  temperature range does not disrupt existing simulations.
  Hence this limited correction, sufficient to verify that it corrects
  the problem on the system that highlighted it,  but not overly
  optimistic as to what it corrects versus what it could deteriorate.
- Fix the `-L` option, which defines a list of surface probes to be
  calculated.
  It was not working correctly with a list containing only one probe.
- Fix the definition of solid properties.
  The size and alignment of the allocated memory were incorrect.
- Fix an invalid memory access when handling errors in the analysis of
  input files with variable expansion.
- Fix an invalid memory access when writing the green function.
- Small improvement of the build system.
  Simplify it by doing everything in one place (the Makefile).
  Add macros to control installation subdirectories

### Version 0.11.1

- Corrects the writing of the RNG state when resolving a probe
  temperature on the boundary: the RNG state was written to the file in
  which the RNG state was read or it was not written at all, depending
  on whether the RNG state to be read was defined or not, respectively.
- Corrects the emissivity of a programmable Dirichlet boundary
  condition: it was set to a constant value, not the programmable value.
- Minor update of stardis man page typesetting.

### Version 0.11

#### Programmable properties

- Custom sampling of conductive paths. On programmable solids, users
  can now define the new `stardis_sampling_conductive_path` function, in
  which they can write their own sampling process. As input, the
  function takes a path whose spatio-temporal position lies within the
  programmable solid. Once the path has been sampled, this position is
  updated and can be either within the solid if the initial condition is
  reached, or at the boundary, i.e.  at a boundary condition. Stardis
  then continues the path sampling procedure in relation to the returned
  state.
- Addition of the property name as the first argument of the input
  string sent to the programmable data creation functions. Not only is
  this useful information, it also respects the convention of program
  arguments. In this way, the standard getopt function can be used
  directly to analyze them.
- Addition of symbolic constants to the public API for programmable
  properties. The constants added are those defining an unknown
  temperature, and constants characterizing null flux or null volumic
  power.

#### Manual pages

- Document the `-n` option which defines the number of realisations. Its
  documentation was missing from the `stardis` manual page.
- Correction of the title of the `stardis-output` manual page. It was
  defined as `stardis-input`.
- Updated the layout of the `stardis-output` manual page. When converted
  to HTML, the width of the columns changed from one list to another,
  resulting in an unstructured layout.

#### Miscellaneous

- Correction of the Dirichlet boundary condition definition. No
  reference temperature was defined on a surface oriented towards a
  fluid and on which a Dirichlet boundary condition was imposed
  (keywords `T_BOUNDARY_FOR_SOLID[_PROG]`). Stardis therefore notified an
  error and rejected radiative trajectories reaching such interfaces
  without a reference temperature. From now on, its reference
  temperature is implicitly defined as the set temperature value.
- Removal of a warning message when calculating a list of boundary
  probes. It incorrectly warned that the side of the boundary on which
  the probe was located was not defined.

### Version 0.10.1

Add a pkg-config file to support the use of Stardis header files such as
the one declaring function profiles for programmable properties.

### Version 0.10

#### New conduction algorithm

Addition of a new algorithm for sampling a potentially unsteady
conductive path. The new `-a` option lets you select the algorithm to be
used, i.e. either the sphere delta algorithm used until now (which is
still the default algorithm), or the new Walk on Sphere algorithm (WoS).

This new algorithm samples an unbiased diffuse trajectory in a solid
with Dirichlet boundary conditions, unbiased with respect to what
numerical accuracy can account for. Its coupling with other boundary or
connection conditions behaves as with the delta sphere algorithm, i.e.
the solution is exact when the length of the trajectory used as a
first-order approximation tends towards 0.

Currently, when using WoS, the initial condition must be constant for
the solid. The power density is also taken into account, but cannot
vary in time and space.

#### External spherical source

An external spherical source can be added to the scene using the new
stardis-input keyword `SPHERICAL_SOURCE`. Once defined, it is considered
as a new boundary condition whose contribution is calculated at the
solid/fluid interfaces in the form of an external net flux.

The external net flux is calculated by evaluating the direct and diffuse
part of the incident flux due to the external source. The diffuse part
of the flux manages not only the radiation from the external source that
reaches the interface after one or more reflections, but also the
external radiation scattered in the environment, here simply represented
by a `diffuse-radiance` parameter.

An external spherical source is defined by its position, radius, power
and diffuse radiance. While the radius is constant, all other parameters
can be programmed using the `SPHERICAL_SOURCE_PROG` keyword. In doing
so, the user can provide a shared object as input, enabling him to
define a time-dependent power and position, and a diffuse radiance that
also depends on the direction along which the sampled trajectory reaches
the environment.

#### Making surface radiative properties source-dependent

Emissivity and specular fraction can now be varied according to the type
of source (internal or external). This is only possible using
programmable properties (`H_BOUNDARY_FOR_FLUID_PROG`,
`HF_BOUNDARY_FOR_SOLID_PROG` and `SOLID_FLUID_PROG`). Properties are
otherwise assumed to be the same for all sources.

#### Make the radiative environment programmable

Addition of the `TRAD_PROG` keyword, which allows the user to define a
radiative temperature and a reference radiative temperature that depend
on a direction.

#### Parallelize the resolutions of multiple boundary probes

Add the `-L` option to calculate multiple boundary probes at once.
Stardis will then parallelize the probe list calculations, rather than
each probe calculation one after the other. Using this option is
therefore more advantageous in terms of load distribution when the
number of boundary probes to be evaluated is large compared to the cost
of calculating a single probe (option `-P`).

#### Allow relative temperatures

Allow to perform calculations relative to a given temperature T. In this
case, the temperatures managed by Stardis would be relative to T and
could therefore be negative, since they would express a deviation from
T. It should be noted that reference temperatures must always be
positive, i.e. expressed in the absolute domain. Finally, we emphasize
that relative calculations only make sense in linear situations, i.e.
negative temperatures are not valid for systems with non-linear
radiative exchanges (i.e. option `-o` whose argument is greater than
one).

This is a file format break that users *must* take into account. Until
now, negative temperatures were considered unknown, whereas they are now
valid. For example, an interface with a negative temperature could be
considered adiabatic, whereas it is now a Dirichlet boundary condition.
In other words, the same data could define completely different systems
before and after this version.

#### Use POSIX make as a build system

The build procedure is now written in POSIX make instead of CMake.
In addition to the features already provided by its CMake alternative,
the Makefile supports the use of static libraries and provides an
uninstall target. In any case, the main motivation behind its writing is
to use a good old well-established standard with simple features,
available on all UNIX systems, thus simplifying its portability and
support while being much lighter

#### Proof-reading and editing manual pages

Write the man pages directly in mdoc's roff macros, instead of using the
asciidoc markup language as a source for man pages.

Unlike writing manuals with man's roff macros, and even more so with
asciidoc, mdoc macros take care of layout, font handling and all the other
typesetting details which, by construction, guarantee the consistency of
all manuals without leaving the responsibility to the individual author.
This also facilitates translation into other formats and documentation
tools. These are the main reasons for writing manual pages with mdoc
macros.

A complete re-reading of the manual pages was carried out during the
translation into mdoc, with several corrections and rewrites to make the
manual clearer.

### Version 0.9

#### Programmable properties

Until now, physical properties as well as boundary and connection
conditions were constant in time and space. In this version, they can be
programmed, i.e.  they are variables returned by functions implemented
in user-defined libraries and submitted as dynamically loaded input
libraries when Stardis starts. User libraries must also provide
create/release functions that are invoked at start-up to allow users to
load their data and build the internal data structures required by their
libraries at run-time.

The `stardis-input` file format has been updated to provide a set of new
`_PROG` suffixed keywords used to define these programmed properties and
conditions (e.g. `T_BOUNDARY_FOR_SOLID_PROG` or
`H_BOUNDARY_FOR_FLUID_PROG`)

#### Miscellaneous

- Addition of the keyword `HF_BOUNDARY_FOR_SOLID` which allows to impose
  a flux on a boundary with another condition. For example, a net flux
  can be defined in addition to a convective exchange and a radiative
  transfer.
- Correct the definition of a net flux as a boundary condition: it might
  not be defined on the right side of the interface.
- Correct the "subpath type" data of the output paths: as we attach the
  segment type to the vertices, we need to locate the type changes along
  the path on zero length segments, otherwise the colouring will show a
  misleading colour gradient.
- Replace the Mersenne Twister random number generator with Threefry:
  the former is much less efficient at rejecting random numbers than the
  latter, which is designed for this purpose, a feature on which
  parallel random number generations depend heavily

### Version 0.8

- Add a new option to support non-linear radiative transfer
  computations.
- Changes in input file's format to support non-linear radiative
  transfer by adding reference temperatures on interfaces.
- Add optional support for MPI (must be enabled at compile time, default
  is OFF).
- Change random number generator type to use Threefry.
- Change the format of binary Green files. A new public header file is
  now installed that describes all types involved in binary Green files.
- Fix a crash on an exit-on-error execution path.
- Fix parsing of command-line options.

### Version 0.7.2

Fix the binary file format of the green function: the fileformat has
been updated without incrementing the version of the serialised data.

### Version 0.7.1

Fix debug build.

### Version 0.7

- Remove the boundary condition `T_BOUNDARY_FOR_FLUID`: it was exactly
  the same than `H_BOUNDARY_FOR_FLUID` that should now be used instead.
- Sets the required version of Star-SampPling to 0.12. This version
  fixes compilation errors with gcc 11 but introduces API breaks.

### Version 0.6

- Add thermal contact resistances.
- Add serialization for random generator's state.
- Bugfixes in arguments parsing.
- Fix Green output file padding.

### Version 0.5.1

- Fix a memleak
- Add a file format version in binary Green files.
- Man improvement.

### Version 0.5

- Ensure C89 compliance.
- New output format for infra-red rendering.
- Use new stardis-solver 0.11.
- Replace fixed dates by time-ranges as time arguments
  for computations.
- Allow unsteady Green's function computations.
- Model files now include scale parameter.

### Version 0.4

- Improve C99 compliance.
- Build on Windows systems.
- Use new stardis-solver 0.5.
- Transition to cmake to manage builds.

### Version 0.3.2

- Add the `solve_probe_boundary` feature. The `solve_probe_boundary` VS
  `solve_probe` selection is automated according the probe-geometry
  distance.  `solve_probe_boundary` is called for probe points closer
  than 2.1 delta from geometry.
- Add flux boundary conditions.

### Version 0.3.1

Add radiative transfer computations. To achieve this, media gain 2 new
parameters:

- emissivity;
- `specular_fraction`.

### Version 0.3

- Upgrade stardis-solver to v0.3.
- Add volumic power sources on solids;
- Allow to use the `fp_to_meter` parameter of the stardis-solver solve
  function;
- Add a dump geometry feature. It outputs the geometry as it is sent to
  stardis-solver in VTK format, together with the front and back media
  and boundary conditions information.


### Version 0.1

- Allow probe computations on conductive-only thermal systems.
- Allow Dirichlet and h.dT boundary conditions.

## License

Copyright (C) 2018-2025 |Méso|Star> (<contact@meso-star.com>)

Stardis is free software released under the GPL v3+ license: GNU GPL
version 3 or later. You are welcome to redistribute it under certain
conditions; refer to the COPYING file for details.
