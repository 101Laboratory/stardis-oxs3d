# Star-enclosures-3d

## Overview

The purpose of Star-enclosures-3d is to extract enclosures from raw
geometry. An enclosure is a set of triangles enclosing a given volume.
The enclosure notion extends to open enclosures for which there is no
inside or outside. For every detected enclosure, the library provides
the set of involved triangle sides as well as the set of involved media
and a few metrics (number of triangles, volume, area, ...).

From release 0.5, the geometry has to be provided at once, with no
duplicates nor zero-area triangles. To help ensuring this constraints,
one can rely on the star-geometry-3d library.

Also the convention regarding FRONT/BACK sides for input triangles as
well as the convention regarding normals' orientation for output
triangles can be set.

## Requirements

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys)
- [Star 3D](https://gitlab.com/meso-star/star-3d)
- [Star 3DUT](https://gitlab.com/meso-star/star-3dut)
  (optinal for tests)
- [Star SamPling](https://gitlab.com/meso-star/star-sp)
  (optinal for tests)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.7.2

- Another correction in the code that groups connex components to create
  enclosures.
- Add function `senc3d_scene_dump_enclosure_obj` which writes an Obj
  file of a given enclosure geometry.

### Version 0.7.1

- Fixes a bug in the code that groups connex components to create
  enclosures.

### Version 0.7

- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.

### Version 0.6

- Major rework on the code that groups connex components to create
  enclosures.
- Add tests showing bugs at the grouping stage in previous release.
- Add debug code that allows to dump how connex components are grouped.
- Fix compilation warnings.
- Sets the required version of Star-3D to 0.9.

### Version 0.5.5

- Fixes a crash linked to numerical accuracy that caused connex
  components grouping to loop forever.

### Version 0.5.4

- Fixes the (rare) situation where temporary local variables could be
  used in a unitialized way in the `senc3d_scene_create` function.
- Sets the required version of Star-SampPling to 0.12. This version
  fixes compilation errors with gcc 11 but introduces API breaks.

### Version 0.5.3

- Fix API break on filter function introduced by Star-3D 0.8.

### Version 0.5.1

- BugFix: enclosures including multiple media could end with invalid
  primitive count.

- BugFix: grouping of components in contact at a single vertex was
  incorrect, causing invalid enclosures.

### Version 0.5

This release breaks the API as geometry cannot be provided through calls
to an add-like call anymore. Instead the whole geometry must be provided
at once, with no duplicates (either vertices or triangles) nor zero-area
triangles.

- Compute volume and surface of enclosures.

- Report overlapping triangles.
  Only triangles with a common edge are currently detected.
  Do not extract enclosures when overlapping triangles are detected.

- Make enclosure IDs consistent across runs.

- Fix enclosure extraction when all triangles Nz component is zero.

- More robust on invalid scenes.

### Version 0.4.2

- Fix global id of triangles; releases 0.4.0 and 0.4.1 are broken

- Reintroduce an API call to get the global id in user space of
  a global unique triangle after deduplication

### Version 0.4.1

- Fix an infinite loop related to a rare numerical accuracy problem.

### Version 0.4

- Change signature of the `senc_scene_add_geometry` API. Thus this
  release is **not compatible** with previous ones.
  The `global_id` callback that was ill-defined is removed and 2
  callbacks are added to manage client-app data when deduplicating
  geometry.
  These 2 callback allow a proper client-app management of global ids. 
- Remove execution time for analysis steps from the log.

### Version 0.3.1

- Performance Fix: when a connex component was canceled by a thread the
  complexity was O(n^2). New algorithm is O(n).
- Output execution time for analysis steps in the log.

### Version 0.3

- Add API calls to access to geometry frontiers.
- Improve documentation in the header file.
- BugFix: wrong data cleaning on computation canceling.

### Version 0.2.2

- BugFix when grouping components into enclosures.
- Add a warning message in log when triangles that could surround a
  hole are found (triangles with at least one unshared edge and
  different media on its sides).

### Version 0.2.1

- BugFix: needed data cleaning on computation canceling.

### Version 0.2

- Add the support of enclosures with multiple media.
- Allow to set the FRONT/BACK convention for input triangles.
- Allow to set the normal convention for output triangles.

## License

Copyright © 2018-2020, 2023, 2024
[|Méso|Star>](https://www.meso-star.com) (<contact@meso-star.com>).

It is free software released under the GPLv3+ license. You are welcome
to redistribute it under certain conditions; refer to the COPYING files
for details.
