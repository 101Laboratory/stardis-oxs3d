# Star Unstructured Volumetric Mesh

Star-UVM is a C library whose goal is to manage unstructured volumetric
meshes defined by a soup of tetrahedra. Once spatially partitioned, the
user can efficiently identify tetrahedra cut by an axis aligned box or
access the specific tetrahedron that encompasses a given position.

Only geometric data is processed by Star-UVM. No assumption is made on
their associated properties. Either way, Star-UVM provides data, such as
primitive indices and barycentric coordinates, needed to retrieve the
properties attached to each tetrahedron. These properties can thus be
managed externally without arbitrary constraints imposed by Star-UVM on
their size, alignment or type.

## Requirements

- C compiler (C99)
- POSIX make
- pkg-config
- [Embree](https://github.com/embree/embree)
- [RSys](https://gitlab.com/vaplv/rsys)
- [Star-Mesh](https://gitlab.com/meso-star/star-mesh) (optional)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.4

- Make suvm-voxelize an installable utility.
- Add the suvm-voxelize man page.

### Version 0.3.1

- In release, replace the -O2 compilation flag by -O3 since significant
  performance gains can be seen (up to 30%) when using level 3 of the C
  optimizer on code that depends heavily on finding the tetrahedron in
  which a given position lies.

### Version 0.3

- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.
- Fix compilation warnings.

### Version 0.2

Update of the acceleration data structure backend from Embree3 to
Embree4

### Version 0.1

- Add the `suvm_mesh_desc_compute_hash` function which uses sha256
  encryption to calculate a digest from the mesh data.
- Add the `suvm_get_mesh_desc` function that returns the mesh data, i.e.
  the list of node positions and the list of cell indexes.
- Replace the obsolete Star-Tetrahedra dependency with Star-Mesh.
- Work around a numerical inaccuracy of barycentric coordinates.
- Fix warnings detected by gcc 11.

## License

Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)

Star-UVM is free software released under the GPL v3+ license: GNU GPL
version 3 or later. You are welcome to redistribute it under certain
conditions; refer to the COPYING file for details.
