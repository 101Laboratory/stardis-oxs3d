# Loader of the Alias Wafefront obj and mtl file formats

AW is a C library whose purpose is to load the Alias Wavefront
[`obj`](http://www.martinreddy.net/gfx/3d/OBJ.spec) and
[`mtl`](http://www.fileformat.info/format/material/) file formats. Only
a subset of these formats are currently supported.

For `obj` files, polygonal primitives with their per vertex-position,
normal and texture coordinates are loaded. Smooth, common and material
groupings are also handled correctly.

For `mtl` files, ambient, diffuse, specular, glossiness and transmissive
RGB or XYZ colors are supported. Additionally, it loads the refraction
index and the illumination model. The library also loads the filename of
ambient, diffuse, specular, glossiness and bump textures and their
attributes (blend< u|v \>, color correction, channel mapping, texcoord
< offset|scale|turbulence \>, image resolution and bump multiplier).

## Requirements

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys/)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 2.1

### POSIX make

Replace CMake by Makefile as build system. The build procedure is
written in POSIX make, which the user can configure via the `config.mk`
file. The POSIX script `make.sh` contains commands that could be found
directly in the Makefile, but which are placed here to simplify its
writing. Finally, a pkg-config file is provided to link the library as
an external dependency.

In addition to the features already provided by its CMake alternative,
the Makefile supports the construction of static libraries, provides an
uninstall target and updates compiler and linker flags to increase the
security and robustness of generated binaries. In any case, the main
motivation for using POSIX make is to rely on a good old
well-established standard with simple functionality, available on all
UNIX systems, thus simplifying its portability and support while being
much lighter.

### Miscellaneous

Update the `aw_obj_desc` data structure: add the number of positions,
texcoords and normals as new member variables of the structure.

### Version 2.0.1

Sets the CMake minimum version to 3.1: since CMake 3.20, version 2.8 has
become obsolete.

### Version 2.0

Full review and refactoring of the code.

- The named groups, the maps and the materials become pod data
  structures without any init/release/copy function. Their name is now
  exposed as a regular `const char*` rather than stored in a `struct
  str` data structure.
- All floating point data are encoded in double precision rather than in
  single precision.
- The `struct aw_obj_vertex` data structure stores only the index of the
  vertex attributes. Their values are retrieved by the new
  `aw_obj_get_vertex_data` function.
- Add an optional stream name parameter to the
  `aw_<obj|mtl>_load_stream` functions: it allows the user to define the
  name of the stream to load, a name that is internally used by the
  logger to print messages regarding the stream.
- Rename the `get` functions.
- Rely on RSys to parse the input files and to perform text conversions.
- Add support of the `map_bump` keyword as an alias of the `bump`
  keyword in the mtl fileformat.

### Version 1.3.1

- Update the version of the RSys dependency to 0.6: replace the
  deprecated `[N]CHECK` macros by the new macro `CHK`.

### Version 1.3

- Add the `aw_obj_purge` and `aw_mtl_purge` functions that not only
  reset the state of the loaders, as the `aw_obj_clear` and
  `aw_mtl_clear` functions, but also free their internal memory.

## License

Copyright (C) 2014-2017, 2020-2023 Vincent Forest (vaplv@free.fr)

AW is free software released under GPL v3+ license: GNU GPL version 3 or
later.  You are welcome to redistribute it under certain conditions;
refer to the COPYING file for details.
