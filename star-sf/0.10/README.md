# Star Scattering Functions

This library provides a set of data structures to describe how light is
scattered on the surface or in volume. From these interfaces, it
implements several Bidirectional Scattering Distribution Functions,
microfacet distributions, Fresnel terms or even phase functions. See the
`ssf.h` header file for a full list of proposed implementations

## Requirements

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys)
- [Star-SP](https://gitlab.com/meso-star/star-sp)
- [RSIMD](https://gitlab.com/vaplv/rsimd) (optional)

## Installation

Edit config.mk as needed, then run

    make clean install

## Release notes

### Version 0.10

- Fix parallel execution of the Makefile for some versions of GNU make.
- Rewrite the Makefile to make it simpler.

### Version 0.9

- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.

### Version 0.8

Added support for discrete phase functions, i.e. phase functions whose
values are only defined for a set of angles.

### Version 0.7.2

Sets the required version of Star-SampPling to 0.12. This version fixes
compilation errors with gcc 11 but introduces API breaks.

### Version 0.7.1

Fix the evaluation of the RDG-FA phase function: the incident direction
was reversed.

### Version 0.7

Add a built-in phase function using the
[RDG-FA](https://doi.org/10.1016/j.jqsrt.2013.08.022) model.

### Version 0.6

- Add the phase function API allowing the user to define, sample and
  evaluate a phase function.
- Provide built-in implementation of the Henyey & Greenstein, and the
  Rayleigh phase functions.

### Version 0.5

- Add the pillbox microfacet distribution.
- Update the version of the RSys dependency to 0.6: replace the
  deprecated `[N]CHECK` macros by the new macro `CHK`.

### Version 0.4

- Fix the Blinn microfacet distribution.
- Change the microfacet distribution API to no longer require the unused
  outgoing direction parameter.
- Use and require Star-SamPling version 0.5.

### Version 0.3

- A BSDF is no more a composition of BxDFs: the caller writes directly
  the comportment of the BSDF without intermediary abstractions. As a
  result, built-in BxDFs become built-in BSDFs and the BxDF data
  structure and functions are removed from the API.

### Version 0.2

- Fix the thin-dielectric material to ensure the energy conservation
  property.
- Add the `ssf_specular_dielectric_dielectric_interface` BxDF. This
  scattering function could be built by combining the
  `ssf_specular_reflection` and the `ssf_specular_transmission` BxDFs
  into a BSDF but such combination does not ensure the energy
  conservation property due to weaknesses into the BSDF interface.

## License

Copyright (C) 2016-2018, 2021-2025 |Méso|Star> (contact@meso-star.com)

Star-SF is free software released under the GPL v3+ license.  You are
welcome to redistribute it under certain conditions; refer to the
COPYING file for details.
