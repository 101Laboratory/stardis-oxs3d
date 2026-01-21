# Star WoS Functions

C library for evaluating and sampling functions used in an unsteady
Walk on Sphere.

## Requirements

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys)

## Installation

Edit config.mk as needed, then run:

    make install

## Release notes

### Version 0.0

Implement the H function as specified in:

"The floating random walk and its application to Monte Carlo solutions
of heat equations" - Haji-Sheikh A. and Sparrow E.M., SIAM Journal on
Applied Mathematics, 14(2): 370-389, 1966

2D and 3D H functions are implemented. Their tabulation is also
implemented to speed up sampling of these functions. When tabulated, the
functions are reconstructed by linear interpolation or (better) by
quadratic extrapolation of the tabulated values. Note that the default
tabulation arguments are carefully defined to guarantee both very good
numerical accuracy and a small memory footprint.

## License

Copyright (C) 2024 |Méso|Star> (contact@meso-star.com)

It is free software released under the GPL v3+ license: GNU GPL version
3 or later. You are welcome to redistribute them under certain
conditions; refer to the COPYING file for details.
