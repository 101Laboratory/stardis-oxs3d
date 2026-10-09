/*
 * ox_s3d_types.h - Internal types for the OptiX s3d wrapper
 *
 * Includes s3d.h for all public types and adds internal-only types
 * (ox_shape_type, ref_T) used by the implementation.
 * This header is NOT part of the public API — include s3d.h instead.
 */
#pragma once

#include "s3d.h"

#include <cstdint>
#include <cstddef>
#include <cfloat>
#include <vector>
#include <map>
#include <functional>

/* ---- Reference Counting (simple, non-atomic) ---- */
typedef unsigned int ref_T;

/* ---- Internal Shape Type Tags ---- */
enum ox_shape_type {
    OX_SHAPE_MESH = 0,
    OX_SHAPE_SPHERE,
    OX_SHAPE_INSTANCE
};
