# Star 3D

Star-3D is a C library that manages *surface geometries* and implements
operators to access them efficiently such as uniform sampling, ray
tracing or a closest point query.

The main concept exposed in Star-3D is the *shape*. A shape represents a
3D surfacic object such as a *triangular mesh* or a *sphere*. Shape data
is user-defined and can be updated at any time. The shapes are then
attached to a *scene* where they can be queried. We point out that a
scene can be *instantiated* once or several times in a new shape which
can be attached to another scene like any other shape. Each instantiated
scene has its own position and orientation while its geometry is stored
once even if the scene is instantiated multiple times. This feature can
thus be used to create an extremely complex environment with a small
memory footprint.

To query scene data, you need to create a *view* of the scene. A view is
actually a scene on which accelerating data structures are built to
accelerate the aforementioned access operators such as ray tracing.
These data structures are constructed from the scene geometry as defined
at the time of view creation. A view is thus insensitive to updates of
the scene following its creation. This means that multiple views can be
used to save different states of the same scene, giving the caller great
flexibility in managing scene data.

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

### Version 0.10

- Replace CMake by Makefile as build system.
- Update compiler and linker flags to increase the security and
  robustness of generated binaries.
- Provide a pkg-config file to link the library as an external
  dependency.

### Version 0.9

Upgrading the ray-tracing backend from Embree3 to Embree4

### Version 0.8.1

Fix compilation warnings with GCC 11

### Version 0.8

Update the API of the filtering function: add the range of the ray as
input argument. For closest point queries, this range is from 0 to query
radius.

### Version 0.7.4

- Fix the barycentric coordinates of the intersection of the ray onto
  the triangle: the coordinates could lie outside the triangle.
- Fix compilation warnings.

### Version 0.7.3

- Fix the `s3d_scene_view_closest_point` function on the scenes
  containing instantiated meshes: the rotation of the instances was
  wrongly taken into account.
- Speed up the `s3d_scene_view_closest_point` query on instantiated
  meshes.

### Version 0.7.2

- Fix a precision issue in the `s3d_scene_view_closest_point` function:
  the returned hit could have invalid barycentric coordinates.

### Version 0.7.1

- Fix an invalid memory read at the setup of the scene view when the
  `S3D_TRACE` flag was set. The Embree backend assumes that the last
  vertex position is at least 16 bytes length while its size was only of
  12 bytes (i.e. 3 single-precision floating-point values).

### Version 0.7

- Add the `s3d_scene_view_closest_point` function. This function
  retrieves the closest point into the scene to a query position in a
  radius from ]0, INF].  This closest point is returned as a regular hit
  which means that the function returns to the caller not only the
  distance to the query position, but also the geometric primitive onto
  which the point lies and its location onto this primitive. If no
  surface lies around the query position in a distance lesser than the
  one defined by the query radius, the returned hit is invalid which can
  be checked with the regular `S3D_HIT_NONE` macro. Finally, this
  function supports the filter functions so that the caller can choose
  to discard closest hits according to its own criteria.
- Add the `s3d_scene_view_create2` function that allows the user to
  configure the acceleration structure used to partition the scene
  geometry. The caller can thus control the quality of the structure and
  thus the compromise between the building time and the query
  performances.
- Improve the performances of the scene bounding box computation. First
  of all, if the scene view is created with the `S3D_TRACE` flag,
  Star-3D directly retrieves the scene bounding box from Embree that
  already computed it to build the acceleration structure. Finally, it
  internally tries to avoid to recompute the bounding box if the scene
  was not updated between 2 scene view creations.

### Version 0.6.2

- Fix an issue in `s3d_scene_view_compute_area`: the returned area was
  wrong when the scene view was created with the `S3D_SAMPLE` flag.

### Version 0.6.1

- Fix an issue in `s3d_scene_view_sample`: the samples were not
  uniformly distributed if the scene contained meshes and spheres.

### Version 0.6

- Migrate the ray-tracing back-end from Embree2 to Embree3.

### Version 0.5.1

- Fix a compilation issue on VC2017.

### Version 0.5

- Add the support of spherical shapes. A sphere is composed of one
  primitive that can be sampled and/or ray-traced as any other
  primitives of the scene.  Hit filtering is also fully supported.

### Version 0.4.2

- Update the version of the RSys dependency to 0.6: replace the
  deprecated `[N]CHECK` macros by the new macro `CHK`.

### Version 0.4.1

- Fix the `s3d_scene_view` consistency when it is created from a scene
  containing instances: the geometries might be not correctly
  synchronised and thus could be outdated.

### Version 0.4

- Implement the `s3d_scene_view` API; it replaces the
  `s3d_scene_<begin|end>_session` functions that were removed. A view
  registers the state of the scene from which it is created. It is used
  to retrieve the scene data through ray-tracing, sampling or indexing.
  Several views can be created on the same scene.
- Add the possibility to attach a same shape to several scenes.
- Fix a memory overconsumption with instantiated shapes: the
  instantiated back-end data were copied rather than shared.
- Add the `s3d_scene_shapes_count` function that returns the overall
  number of shapes in the scene.
- Add the `s3d_instance_set_transform` and `s3d_instance_transform`
  functions that sets or gets the transformation matrix of the instance,
  respectively.
- Add the `s3d_primitive_get_transform` function that gets the
  transformation matrix of a primitive.
- Add the `s3d_primitive_has_attrib` function that returns if a
  primitive has a specific attribute or not.
- Add the `s3d_triangle_get_vertex_attrib` function that retrieves the
  vertex attributes of a triangular primitive.

## License

Copyright (C) 2015-2023 |Méso|Star> (contact@meso-star.com)

Star-3D is free software released under GPL v3+ license: GNU GPL version
3 or later. You are welcome to redistribute it under certain conditions;
refer to the COPYING file for details.
