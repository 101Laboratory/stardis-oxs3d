# Star-VoXel

Star-VX is a C library designed to manage volume elements, called
voxels, structured as a set of axis-aligned cuboids. This library knows
nothing about the volume data it manages: it only provides data
structures that partition the voxels according to the user's criteria.
It also implements efficient operators for indexing voxels in space
partitioning data structures or for accessing them by ray tracing.

Star-VX offers 2 hierarchical data structures: the *octree* and the
*binary tree*. The former is used to structure regular 3D data, while
the latter is used for 1D data. Star-VX builds the required data
structure following a bottom-up strategy: the user submits the data set
to be partitioned, as well as the policy used to define when voxels can
be merged, and the merge operator itself. The way in which data is
accessed by *indexing* or *ray tracing* can also be finely tuned by the
user: in addition to the position of the probe or the radius to be
traced, he can provide callbacks to stop traversal at a hierarchical
level that is not a leaf, perform calculations at the traversed leaf
level, reject leaves, and so on. The host application thus has total
control over the traversal of the data structure, while its in-memory
representation and accessors are still entirely managed internally by
Star-VX.

## Requirements

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.3.1

- Fix ray-tracing inaccuracy in a binary tree when the ray direction is
  approximately aligned with the infinite dimension.
- Improve the build system.
  Simplify it by doing everything in one place (the Makefile).
  Add macros to control installation sub-directories.

### Version 0.3

- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.
- Fix compilation warnings

### Version 0.2.1

Sets the CMake minimum version to 3.1: since CMake 3.20, version 2.8 has
become obsolete.

### Version 0.2

- Update the profile of the functors invoked on node traversal: the ray
  origin, direction and range are now provided as input arguments.
- Force the invocation of the challenge and filter functors on the root
  node.  Previously these functors were not called on the root;the
  challenging and filtering was starting on its children.

### Version 0.1

- Add the `svx_tree_write` and the `svx_tree_create_from_stream`
  functions used to serialize and de-serialize the tree data structure,
  respectively.

## License

Copyright (C) 2018, 2020-2025 |Méso|Star> (contact@meso-star.com)  
Copyright (C) 2018 Université Paul Sabatier

Star-VX is free software released under the GPL v3+ license: GNU GPL
version 3 or later. You are welcome to redistribute it under certain
conditions; refer to the COPYING file for details.
