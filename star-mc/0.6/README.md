# Star Monte Carlo

Star-MonteCarlo tries to simplify the calculation of the estimate of a
user-defined integrator. It ensures the idenpendance of random sequences
between threads and automates the calculation of expectation, variance
and standard error.

## Requirements to build

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys)
- [Star-SP](https://gitlab.com/meso-star/star-sp)

## How to build

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.6

- Update `smc_device_create` profile: input arguments are grouped
  together in the `smc_device_create_args` structure. In addition to the
  existing input arguments, it contains a new `verbose` member variable
  which controls the library's verbosity level. By default, the library
  is silent.
- Add null constants for the following smc data structures: `smc_type`,
  `smc_estimator_status`, `smc_integrator` and `smc_doubleN_context`.
- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.

### Version 0.5

Sets the required version of Star-SampPling to 0.12.1. The versions 0.12
and highers fix compilation errors with gcc 11 but introduces API
breaks: the `ssp_rng_type` is no longer a structure but becomes an
enumeration.

### Version 0.4.1

- Add a progress log during computations.
- Sets the CMake minimum version to 3.1: since CMake 3.20, version 2.8
  has become obsolete.

### Version 0.4

- Remove the raw integration functionality.
- Rename the `max_steps` integrator attribute in `max_realisations`.
- Update the profile of the integrand function of the integrator: the
  identifier of the thread that invokes the integrand is provided has an
  input parameter.
- Add the `smc_device_get_threads_count` function that returns the
  maximum number of threads used by the device.

## License

Copyright (C) 2015-2018, 2021-2023 |Méso|Star> (contact@meso-star.com)

Star-MC is free software released under GPL v3+ license: GNU GPL version
3 or later. You are welcome to redistribute it under certain conditions;
refer to the COPYING file for details.
