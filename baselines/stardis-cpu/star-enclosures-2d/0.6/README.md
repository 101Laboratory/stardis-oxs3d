# Star-enclosures-2d

The purpose of Star-enclosures-2d is to extract enclosures from raw
geometry. An enclosure is a set of segments enclosing a given area. The
enclosure notion extends to open enclosures for which there is no inside
or outside. For every detected enclosure, the library provides the set
of involved segment sides as well as the set of involved media and a few
metrics (number of segments, volume, area, ...).

From release 0.5, the geometry has to be provided at once, with no
duplicates nor zero-length segments. To help ensuring this constraints,
one can rely on the star-geometry-2d library.

Also the convention regarding FRONT/BACK sides for input segments as
well as the convention regarding normals' orientation for output
segments can be set.

## Requirements

- C compiler
- POSIX make
- pkg-config
- [RSys](https://gitlab.com/vaplv/rsys)
- [Star 2D](https://gitlab.com/meso-star/star-3d)
- [Star SamPling](https://gitlab.com/meso-star/star-sp)
  (optinal for tests)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Known Bugs

In some rare circumstances, raytracing accuracy problems can lead to
incorrect connex components pairing. This bug has already been fixed
in star-enclosures-3d but is still present in star-enclosures-2d.

## Release notes

### Version 0.6

- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.

### Version 0.5.5

Fixes the (rare) situation where temporary local variables could be used
in a unitialized way in the `senc2d_scene_create` function.

### Version 0.5.4

Sets the required version of Star-SampPling to 0.12. This version fixes
compilation errors with gcc 11 but introduces API breaks.

### Version 0.5.3

Fix API break on filter function introduced by Star-2D 0.5.

### Version 0.5.2

BugFix: enclosures including multiple media could end with invalid
primitive count.

### Version 0.5.1

Fix a warning

### Version 0.5

This release breaks the API as geometry cannot be provided through calls
to an add-like call anymore. Instead the whole geometry must be provided
at once, with no duplicates (either vertices or segments) nor zero-area
segments.

- Compute volume (in m^2) and surface (in m) of enclosures.
- Report overlapping segments. Only segments with a common vertex are
  currently detected. Do not extract enclosures when overlapping
  segments are detected.
- Make enclosure IDs consistent across runs.
- Fix enclosure extraction when all segments Ny component is zero.
- More robust on invalid scenes.

### Version 0.4.2

- Fix global id of segments; releases 0.4.0 and 0.4.1 are broken
- Reintroduce an API call to get the global id in user space of a global
  unique segment after deduplication

### Version 0.4.1

- Fix an infinite loop related to a rare numerical accuracy problem.

### Version 0.4

- Change signature of the `senc2d_scene_add_geometry` API. Thus this
  release is **not compatible** with previous ones. The `global_id`
  callback that was ill-defined is removed and 2 callbacks are added to
  manage client-app data when deduplicating geometry. These 2 callback
  allow a proper client-app management of global ids.
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
- Add a warning message in log when edges that could surround a hole are
  found (edges with at least one unshared vertex and different media on
  its sides).

### Version 0.2.1

BugFix: needed data cleaning on computation canceling.

### Version 0.2

- Add the support of enclosures with multiple media.
- Allow to set the FRONT/BACK convention for input segments.
- Allow to set the normal convention for output segments.

## License

Copyright © 2018-2021, 2023, 2024
[|Méso|Star>](https://www.meso-star.com) (<contact@meso-star.com>).

Star-enclosures-2d is free software released under the GPLv3+ license:
GNU GPL version 3 or later. You are welcome to redistribute it under
certain conditions; refer to the COPYING file for details.
