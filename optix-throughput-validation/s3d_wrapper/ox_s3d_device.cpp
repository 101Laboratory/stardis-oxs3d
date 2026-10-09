/*
 * ox_s3d_device.cpp — s3d_device lifecycle (create/ref_get/ref_put + GPU SM query)
 *
 * Wraps DeviceManager (CUDA + OptiX context) behind the s3d_device handle.
 * The PTX files are loaded lazily: they are read from the same directory
 * as the executable (or via OPTIX_PTX_FILE_PATH / OPTIX_NN_PTX_FILE_PATH
 * compile-time definitions).
 */

#include "ox_s3d_internal.h"

#include <fstream>
#include <iostream>
#include <cstdlib>

/* ================================================================
 * PTX Helpers
 * ================================================================ */

static std::string load_ptx_file(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f.is_open()) return std::string();
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

static std::string find_and_load_ptx(const char* compile_time_path,
                                     const char* filename) {
    std::vector<std::string> search;
    if (compile_time_path && compile_time_path[0])
        search.push_back(compile_time_path);
    search.push_back(std::string("./") + filename);
    search.push_back(std::string("../") + filename);
    search.push_back(filename);

    for (auto& p : search) {
        std::string ptx = load_ptx_file(p);
        if (!ptx.empty()) return ptx;
    }
    return std::string();
}

/* ================================================================
 * s3d_device_create
 * ================================================================ */
res_T s3d_device_create(struct logger*  /*logger*/,
                        struct mem_allocator*  /*allocator*/,
                        const int    verbose,
                        s3d_device** out_dev)
{
    if (!out_dev) return RES_BAD_ARG;
    *out_dev = nullptr;

    s3d_device* dev = new (std::nothrow) s3d_device();
    if (!dev) return RES_MEM_ERR;

    dev->verbose = verbose;

    try {
        dev->device_manager.init(0, verbose > 2);

        /* Load PTX sources */
#ifdef OPTIX_PTX_FILE_PATH
        const char* rt_path = OPTIX_PTX_FILE_PATH;
#else
        const char* rt_path = nullptr;
#endif
#ifdef OPTIX_NN_PTX_FILE_PATH
        const char* nn_path = OPTIX_NN_PTX_FILE_PATH;
#else
        const char* nn_path = nullptr;
#endif
        dev->rt_ptx = find_and_load_ptx(rt_path, "programs.ptx");
        dev->nn_ptx = find_and_load_ptx(nn_path, "nn_programs.ptx");

        if (dev->rt_ptx.empty() || dev->nn_ptx.empty()) {
            if (verbose > 0)
                std::cerr << "[ox_s3d] WARNING: PTX not found; init deferred\n";
        }

        dev->initialized = true;
        if (verbose > 0) {
            dev->device_manager.printDeviceInfo();
        }
    } catch (const std::exception& e) {
        std::cerr << "[ox_s3d] device_create failed: " << e.what() << "\n";
        delete dev;
        return RES_UNKNOWN_ERR;
    }

    *out_dev = dev;
    return RES_OK;
}

/* ================================================================
 * Ref counting
 * ================================================================ */
res_T s3d_device_ref_get(s3d_device* dev) {
    if (!dev) return RES_BAD_ARG;
    dev->ref++;
    return RES_OK;
}

res_T s3d_device_ref_put(s3d_device* dev) {
    if (!dev) return RES_BAD_ARG;
    if (dev->ref == 0) return RES_BAD_ARG;
    fprintf(stderr, "[DBG] device_ref_put dev=%p ref=%u->%u\n",
            (void*)dev, (unsigned)dev->ref, (unsigned)(dev->ref - 1));
    fflush(stderr);
    if (--dev->ref == 0) {
        fprintf(stderr, "[DBG]   calling shutdown...\n"); fflush(stderr);
        dev->device_manager.shutdown();
        fprintf(stderr, "[DBG]   shutdown OK, deleting device...\n"); fflush(stderr);
        delete dev;
        fprintf(stderr, "[DBG]   device deleted OK\n"); fflush(stderr);
    }
    return RES_OK;
}

/* ================================================================
 * GPU SM count
 * ================================================================ */
int s3d_device_get_gpu_sm_count(s3d_device* dev) {
    if (!dev || !dev->initialized) return 0;
    int sm = 0;
    cudaDeviceGetAttribute(&sm, cudaDevAttrMultiProcessorCount,
                           dev->device_manager.getDeviceId());
    return sm;
}
