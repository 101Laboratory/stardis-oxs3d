# Star 2D

Star-2D is a C library that manages *2D geometries* and implements
operators to access them efficiently such as uniform sampling, ray
tracing or a closest point query.

The main concept exposed in Star-2D is the *shape*. A shape is a set of
segments representing the outline of a 2D geometry. Shape data is
user-defined and can be updated at any time. The shapes are then
attached to a *scene* where they can be queried using a *view* of the
scene. When it is created, the view internally builds the accelerating
structures necessary for the access operators. These data structures are
constructed from the scene geometry as defined at the time of view
creation; a view is thus insensitive to updates of the scene following
its creation. This means that multiple views can be used to save
different states of the same scene, giving the caller great flexibility
in managing scene data.

## Requirements

- C compiler (C99)
- POSIX make
- pkg-config
- [Embree](https://github.com/embree/embree)
- [RSys](https://gitlab.com/vaplv/rsys)

## Installation

Edit config.mk as needed, then run:

    make clean install

## Release notes

### Version 0.7

- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.

### Version 0.6

- Upgrading the ray-tracing backend from Embree3 to Embree4

### Version 0.5.1

- Corrects an invalid memory read.
- Fix compilation warnings detected by gcc 11.

### Version 0.5

Update the API of the filtering function: add the range of the ray as
input argument. For closest point queries, this range is from 0 to query
radius.

### Vesion 0.4.1

- Improve the accuracy of the closest point query.
- Fix the closest point query: the returned hit was wrong.
- Fix the barycentric coordinates of the intersection of the ray onto
  the segment: the coordinates could lie outside the segment.

### Version 0.4

- Add the `s2d_scene_view_closest_point` function. This function
  retrieves the closest point into the scene to a query position in a
  radius from ]0, INF].  This closest point is returned as a regular hit
  which means that the function returns to the caller not only the
  distance to the query position, but also the geometric primitive onto
  which the point lies and its location onto this primitive. If no
  segment lies around the query position in a distance lesser than the
  one defined by the query radius, the returned hit is invalid which can
  be checked with the regular `S2D_HIT_NONE` macro. Finally, this
  function supports the filter functions so that the caller can choose
  to discard closest hits according to its own criteria.

### Version 0.3.1

- Fix the `s2d_segment_get_vertex_attrib` function: it could fail and
  return an error while it shouldn't.

### Version 0.3

- Migrate the ray-tracing backend from Embree2 to Embree3.

### Version 0.2

- Add the `s2d_segment_get_vertex_attrib` function that returns the
  vertex attributes of a given segment.

### Version 0.1.1

- Fix the `s2d_primitive_get_function`. The function failed when the
  submitted parametric coordinate was equal to 1.

### Version 0.1

- Implement the `s2d_scene_view` API; it replaces the
  `s2d_scene_<begin|end>_session` functions that were removed. A view
  registers the state of the scene from which it is created. It is used
  to retrieve the scene data through ray-tracing, sampling or indexing.
  Several views can be created on the same scene.
- Add the possibility to attach a same shape to several scenes.

## License

Copyright (C) 2016-2021, 2023 |Méso|Star> (contact@meso-star.com)

Star-2D is free software released under GPL v3+ license: GNU GPL version
3 or later. You are welcome to redistribute it under certain conditions;
refer to the COPYING file for details.
