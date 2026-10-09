# Star-Blackbody

This C library provides functions for manipulating the Planck function:
calculating its monochromatic value or its integral over an arbitrary
spectral interval. It also lets you invert these quantities in terms of
brightness temperature, and perform spectral sampling using Planck's
function as a probability density function.

## Requirements

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys)

## Installation

Edit config.mk as needed, then run:

    make clean install

## License

Copyright (C) 2018-2023 |Méso|Star> (contact@meso-star.com)

Star-Blackbody is free software released under the GPL v3+ license: GNU
GPL version 3 or later. You are welcome to redistribute it under certain
conditions; refer to the COPYING file for details.
