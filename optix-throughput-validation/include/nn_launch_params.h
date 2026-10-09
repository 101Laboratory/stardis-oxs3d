/*
 * nn_launch_params.h - OptiX launch parameters for nearest neighbor queries
 * Shared between host code and OptiX device programs (nn_programs.cu).
 */
#pragma once

#include "nn_types.h"

#ifdef __CUDACC__
#include <optix.h>
#else
#include <optix_types.h>
#endif

/* ---- NN Launch Parameters ---- */
struct NNLaunchParams {
    OptixTraversableHandle handle;       /* GAS of point-cloud AABBs        */
    float3*       queries;               /* device buffer: query positions   */
    float3*       points;                /* device buffer: point cloud data  */
    NNResult*     results;               /* device buffer: output results    */
    unsigned int  num_queries;           /* number of queries in this launch */
};
