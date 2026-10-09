# htpp

This tool post-processes images saved in htrdr-image format.

## Requirements

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys)
- [Star Color Map](https://gitlab.com/meso-star/star-cmap)
- [mandoc](https://mandoc.bsd.lv)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.5

- Write the man page directly in mdoc's roff macros, instead of using
  the intermediate scdoc source.
- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.

### Version 0.4.4

Fix a typo in the man page

### Version 0.4.3

- Add list of available palettes to man page.
- Use scdoc rather than asciidoc as file format for man sources.
- Remove deprecated MSVC support.

### Version 0.4.2

- Fix a wrong assertion on the range of the data to display: in debug
  `htpp` was stopping and reporting an error if the data to display all
  had the same value.
- Fix the CMake file for MSVC build.

### Version 0.4.1

- Fix MSVC build.
- Change install dir for html man.
- Stop building html man on Unix systems (still optionally available).

### Version 0.4

- Add the `gnuplot` parameter to the `-m` option. Once set, the result
  image is written as a gnuplot script rather than a PPM image. This
  script generates a PNG image with an embedded color ramp.
- Fix the tone map operator: the exposure term is now also applied to
  the white radiance to ensure that it effectively corresponds to the
  white color in the output image.

### Version 0.3

- Add the `-i` option that regroups all the options controlling the
  post-processing of the pixel color. The options `-e` and `-w` that
  defined the exposure and the white scale factor are thus removed.
- Add support of raw data visualisation through color palettes. The `-m`
  option enables the mapping of a pixel component to a color ramp. Both,
  the pixel component to visualise and the palette to use can be set
  through this option.  The data range to visualise can also be fixed.
  When this option is defined, the `-v` option can be used to displays
  into the terminal the color ramp and its associated values.
- Remove the `-u` and `-T` options that are previously used to roughly
  visualise the per pixel uncertainties and the estimate of the per
  realisation time: the new `-m` option proposes a far more efficient
  alternative.

### Version 0.2.1

- Update how the image exposure is handled: the pixels are multiplied by
  the exposure priorly to their tone mapping. Previously, it was the
  tone mapped pixels that were multiplied by the exposure.
- Fix the man page that wrongly described the tone mapping operator.

### Version 0.2

- Fix the XYZ to sRGB conversion: the reference white was not correctly
  set.
- Fix the gamma correction of the linear sRGB color: there was an issue
  in the used formulae.

### Version 0.1

- Handle the update of the htrdr-image file format introduced by
  [htrdr](https://gitlab.com/meso-star/htrdr/) 0.1 that adds to each
  pixel the estimation of the per realisation path computation time.

## Copyright notice

Copyright © 2018-2020, 2023 |Méso|Star> (contact@meso-star.com)  
Copyright © 2018, 2019 Centre National de la Recherche Scientifique  
Copyright © 2018, 2019 Université Paul Sabatier

## License

htpp is free software released under the GPL v3+ license: GNU GPL
version 3 or later. You are welcome to redistribute it under certain
conditions; refer to the COPYING file for details.
