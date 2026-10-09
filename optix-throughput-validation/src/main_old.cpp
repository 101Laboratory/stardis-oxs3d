/*
 * main.cpp - OptiX 9.1.0 Throughput Benchmark
 *
 * Tests maximum batch ray tracing throughput across:
 *   1. Different ray counts (1K → 64M)
 *   2. Different scene complexities (1 tri → 1M tris)
 *   3. Single-ray vs batch tracing
 *   4. GAS-only vs IAS+GAS configurations
 *   5. Nearest neighbor queries via RT core AABB traversal
 *
 * Usage:
 *   optix_throughput [options]
 *     --validate       Run validation tests first
 *     --max-rays <N>   Maximum ray count (default: 64M)
 *     --iters <N>      Timed iterations per test (default: 10)
 *     --device <id>    CUDA device ID (default: 0)
 */

#include "device_manager.h"
#include "buffer_manager.h"
#include "geometry_manager.h"
#include "accel_manager.h"
#include "pipeline_manager.h"
#include "ray_tracer.h"
#include "nn_query.h"
#include "optix_check.h"
#include "ray_types.h"
#include "nn_types.h"
#include "launch_params.h"
#include "kernels.h"
#include "nn_kernels.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <chrono>
#include <algorithm>

/* ======================================================================
 * PTX File Loading
 * ====================================================================*/
static std::string loadPtxFile(const std::string& filename)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open PTX file: " + filename);
    }
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

/* Try multiple paths to find the PTX file */
static std::string findAndLoadPtx()
{
    std::vector<std::string> search_paths = {
        "programs.ptx",
        "../programs.ptx",
        "../../programs.ptx",
#ifdef OPTIX_PTX_FILE_PATH
        OPTIX_PTX_FILE_PATH,
#endif
    };

    for (const auto& path : search_paths) {
        std::ifstream test(path);
        if (test.good()) {
            test.close();
            std::cout << "  Found PTX: " << path << "\n";
            return loadPtxFile(path);
        }
    }

    throw std::runtime_error(
        "Could not find programs.ptx. Searched:\n"
        "  - programs.ptx (current dir)\n"
        "  - ../programs.ptx\n"
        "  - ../../programs.ptx\n"
        "  - OPTIX_PTX_FILE_PATH compile-time path\n"
        "Make sure the project is built successfully first.");
}

/* Try multiple paths to find the NN PTX file */
static std::string findAndLoadNNPtx()
{
    std::vector<std::string> search_paths = {
        "nn_programs.ptx",
        "../nn_programs.ptx",
        "../../nn_programs.ptx",
#ifdef OPTIX_NN_PTX_FILE_PATH
        OPTIX_NN_PTX_FILE_PATH,
#endif
    };

    for (const auto& path : search_paths) {
        std::ifstream test(path);
        if (test.good()) {
            test.close();
            std::cout << "  Found NN PTX: " << path << "\n";
            return loadPtxFile(path);
        }
    }

    throw std::runtime_error(
        "Could not find nn_programs.ptx. Searched:\n"
        "  - nn_programs.ptx (current dir)\n"
        "  - ../nn_programs.ptx\n"
        "  - ../../nn_programs.ptx\n"
        "  - OPTIX_NN_PTX_FILE_PATH compile-time path\n"
        "Make sure the project is built successfully first.");
}

/* ======================================================================
 * Helper: Generate random point cloud on CPU
 * ====================================================================*/
static std::vector<float3> generateRandomPointCloud(
    unsigned int num_points,
    float        bbox_extent = 10.0f,
    unsigned int seed        = 42)
{
    std::vector<float3> points(num_points);
    float half = bbox_extent * 0.5f;

    /* Simple LCG for reproducibility */
    unsigned int rng = seed;
    auto next_float = [&rng]() -> float {
        rng = rng * 747796405u + 2891336453u;
        unsigned int word = ((rng >> ((rng >> 28u) + 4u)) ^ rng) * 277803737u;
        word = (word >> 22u) ^ word;
        return (float)word / (float)0xFFFFFFFFu;
    };

    for (unsigned int i = 0; i < num_points; ++i) {
        points[i] = make_float3(
            next_float() * bbox_extent - half,
            next_float() * bbox_extent - half,
            next_float() * bbox_extent - half);
    }
    return points;
}

/* ======================================================================
 * Helper: Format large numbers with commas
 * ====================================================================*/
static std::string formatNumber(unsigned int n)
{
    std::string s = std::to_string(n);
    int pos = (int)s.length() - 3;
    while (pos > 0) {
        s.insert(pos, ",");
        pos -= 3;
    }
    return s;
}

static std::string formatNumberShort(unsigned int n)
{
    if (n >= 1000000) return std::to_string(n / 1000000) + "M";
    if (n >= 1000)    return std::to_string(n / 1000) + "K";
    return std::to_string(n);
}

/* ======================================================================
 * Print section header
 * ====================================================================*/
static void printHeader(const std::string& title)
{
    std::cout << "\n" << std::string(72, '=') << "\n"
              << "  " << title << "\n"
              << std::string(72, '=') << "\n";
}

static void printSubHeader(const std::string& title)
{
    std::cout << "\n--- " << title << " ---\n";
}

/* ======================================================================
 * Validation: Single ray tracing correctness check
 * ====================================================================*/
static bool runValidation(RayTracer& tracer)
{
    printSubHeader("Validation: Single Ray Tracing");

    /* Test 1: Ray that should hit the triangle */
    Ray ray_hit;
    ray_hit.origin    = make_float3(0.0f, 0.0f, -2.0f);
    ray_hit.direction = make_float3(0.0f, 0.0f, 1.0f);
    ray_hit.tmin      = 0.0f;
    ray_hit.tmax      = 1e16f;

    HitResult result = tracer.traceSingle(ray_hit);
    bool hit_ok = (result.t > 0.0f);
    std::cout << "  Hit test:  t=" << result.t
              << " bary=(" << result.bary_u << ", " << result.bary_v << ")"
              << " prim=" << result.prim_idx
              << (hit_ok ? " [PASS]" : " [FAIL]") << "\n";

    /* Test 2: Ray that should miss */
    Ray ray_miss;
    ray_miss.origin    = make_float3(100.0f, 100.0f, -2.0f);
    ray_miss.direction = make_float3(0.0f, 0.0f, 1.0f);
    ray_miss.tmin      = 0.0f;
    ray_miss.tmax      = 1e16f;

    result = tracer.traceSingle(ray_miss);
    bool miss_ok = (result.t < 0.0f);
    std::cout << "  Miss test: t=" << result.t
              << (miss_ok ? " [PASS]" : " [FAIL]") << "\n";

    /* Test 3: Batch trace basic */
    printSubHeader("Validation: Batch Ray Tracing");
    std::vector<Ray> test_rays(100);
    for (int i = 0; i < 100; ++i) {
        test_rays[i].origin    = make_float3(
            -0.4f + 0.8f * (i % 10) / 10.0f,
            -0.4f + 0.8f * (i / 10) / 10.0f,
            -2.0f);
        test_rays[i].direction = make_float3(0.0f, 0.0f, 1.0f);
        test_rays[i].tmin      = 0.0f;
        test_rays[i].tmax      = 1e16f;
    }

    auto results = tracer.traceBatch(test_rays);
    int num_hits = 0;
    for (const auto& r : results) {
        if (r.t >= 0.0f) num_hits++;
    }
    bool batch_ok = (num_hits > 0);
    std::cout << "  Batch (100 rays): " << num_hits << " hits / 100 rays"
              << (batch_ok ? " [PASS]" : " [FAIL]") << "\n";

    return hit_ok && miss_ok && batch_ok;
}

/* ======================================================================
 * Benchmark: Ray Count Sweep
 * ====================================================================*/
static void benchmarkRayCountSweep(
    RayTracer&                     tracer,
    const std::vector<unsigned int>& ray_counts,
    float3                         bbox_min,
    float3                         bbox_max,
    int                            num_iters,
    const std::string&             scene_name,
    std::ostream&                  csv)
{
    printSubHeader("Ray Count Sweep - " + scene_name);

    std::cout << std::right
              << std::setw(12) << "Rays"
              << std::setw(14) << "Time(ms)"
              << std::setw(14) << "MRays/s"
              << std::setw(12) << "GRays/s"
              << std::setw(10) << "Hit%"
              << "\n"
              << std::string(62, '-') << "\n";

    for (unsigned int count : ray_counts) {
        /* Allocate ray and hit buffers */
        CudaBuffer<Ray>       d_rays;
        CudaBuffer<HitResult> d_hits;
        d_rays.alloc(count);
        d_hits.alloc(count);

        /* Generate random rays on GPU */
        generateRandomRaysDevice(d_rays.get(), count, bbox_min, bbox_max, 12345);
        CUDA_CHECK(cudaDeviceSynchronize());

        /* Timed trace */
        TraceResult result = tracer.traceBatchTimed(
            d_rays.get(), d_hits.get(), count, num_iters, 3);

        std::cout << std::right
                  << std::setw(12) << formatNumber(count)
                  << std::setw(14) << std::fixed << std::setprecision(3) << result.trace_time_ms
                  << std::setw(14) << std::fixed << std::setprecision(2) << result.mrays_per_sec
                  << std::setw(12) << std::fixed << std::setprecision(4) << result.grays_per_sec
                  << std::setw(9)  << std::fixed << std::setprecision(1) << (result.hit_rate * 100.0f) << "%"
                  << "\n";

        csv << scene_name << ","
            << count << ","
            << result.trace_time_ms << ","
            << result.mrays_per_sec << ","
            << result.grays_per_sec << ","
            << result.hit_rate << "\n";
    }
}

/* ======================================================================
 * Benchmark: Scene Complexity Sweep
 * ====================================================================*/
static void benchmarkSceneComplexity(
    DeviceManager&    device,
    PipelineManager&  pipeline,
    unsigned int      fixed_ray_count,
    int               num_iters,
    std::ostream&     csv)
{
    printSubHeader("Scene Complexity Sweep (" + formatNumber(fixed_ray_count) + " rays)");

    std::cout << std::right
              << std::setw(12) << "Triangles"
              << std::setw(14) << "Build(ms)"
              << std::setw(14) << "Trace(ms)"
              << std::setw(14) << "MRays/s"
              << std::setw(12) << "GRays/s"
              << std::setw(10) << "Hit%"
              << "\n"
              << std::string(76, '-') << "\n";

    /* Test different triangle counts */
    std::vector<unsigned int> tri_counts = {
        1, 100, 1000, 10000, 100000, 500000, 1000000
    };

    for (unsigned int tri_count : tri_counts) {
        /* Generate geometry */
        TriangleMesh mesh;
        if (tri_count == 1) {
            mesh = GeometryManager::createSingleTriangle();
        } else if (tri_count <= 100) {
            mesh = GeometryManager::createCornellBox();
            tri_count = (unsigned int)mesh.indices.size();
        } else {
            mesh = GeometryManager::createRandomTriangles(tri_count);
        }

        /* Time the accel build */
        auto build_start = std::chrono::high_resolution_clock::now();

        AccelManager accel;
        AccelStructure gas = accel.buildGAS(device.getContext(), mesh);

        auto build_end   = std::chrono::high_resolution_clock::now();
        float build_ms   = std::chrono::duration<float, std::milli>(build_end - build_start).count();

        /* Setup ray tracer */
        RayTracer tracer;
        tracer.init(pipeline.getPipeline(), pipeline.getSBT(), gas.handle);

        /* Allocate and generate rays */
        CudaBuffer<Ray>       d_rays;
        CudaBuffer<HitResult> d_hits;
        d_rays.alloc(fixed_ray_count);
        d_hits.alloc(fixed_ray_count);

        generateRandomRaysDevice(d_rays.get(), fixed_ray_count,
                                 mesh.bbox_min, mesh.bbox_max, 42);
        CUDA_CHECK(cudaDeviceSynchronize());

        /* Timed trace */
        TraceResult result = tracer.traceBatchTimed(
            d_rays.get(), d_hits.get(), fixed_ray_count, num_iters, 3);

        std::cout << std::right
                  << std::setw(12) << formatNumber(tri_count)
                  << std::setw(14) << std::fixed << std::setprecision(3) << build_ms
                  << std::setw(14) << std::fixed << std::setprecision(3) << result.trace_time_ms
                  << std::setw(14) << std::fixed << std::setprecision(2) << result.mrays_per_sec
                  << std::setw(12) << std::fixed << std::setprecision(4) << result.grays_per_sec
                  << std::setw(9)  << std::fixed << std::setprecision(1) << (result.hit_rate * 100.0f) << "%"
                  << "\n";

        csv << "complexity," << tri_count << ","
            << build_ms << ","
            << result.trace_time_ms << ","
            << result.mrays_per_sec << ","
            << result.grays_per_sec << ","
            << result.hit_rate << "\n";
    }
}

/* ======================================================================
 * Benchmark: Instance Acceleration (IAS)
 * ====================================================================*/
static void benchmarkInstancing(
    DeviceManager&   device,
    unsigned int     fixed_ray_count,
    int              num_iters,
    std::ostream&    csv)
{
    printSubHeader("IAS Instancing Test (" + formatNumber(fixed_ray_count) + " rays)");

    /* Create a base mesh (Cornell Box) */
    TriangleMesh base_mesh = GeometryManager::createCornellBox();

    /* Build the base GAS */
    AccelManager accel;
    AccelStructure base_gas = accel.buildGAS(device.getContext(), base_mesh);

    /* Create instances with different transforms */
    std::vector<unsigned int> instance_counts = { 1, 4, 16, 64, 256 };

    /* Need a separate pipeline for instancing */
    std::string ptx_source = findAndLoadPtx();
    PipelineManager ias_pipeline;
    ias_pipeline.create(device.getContext(), ptx_source, 4, 2, 1, true /* use_instancing */);

    std::cout << std::right
              << std::setw(12) << "Instances"
              << std::setw(14) << "Build(ms)"
              << std::setw(14) << "Trace(ms)"
              << std::setw(14) << "MRays/s"
              << std::setw(12) << "GRays/s"
              << "\n"
              << std::string(66, '-') << "\n";

    for (unsigned int num_instances : instance_counts) {
        /* Create instance transforms (grid layout) */
        std::vector<InstanceDesc> instances(num_instances);
        int grid = (int)std::ceil(std::sqrt((double)num_instances));

        for (unsigned int i = 0; i < num_instances; ++i) {
            int ix = i % grid;
            int iy = i / grid;
            float offset_x = (ix - grid / 2.0f) * 3.0f;
            float offset_y = (iy - grid / 2.0f) * 3.0f;

            /* Identity rotation + translation */
            float transform[12] = {
                1, 0, 0, offset_x,
                0, 1, 0, offset_y,
                0, 0, 1, 0
            };

            instances[i].gas_handle      = base_gas.handle;
            memcpy(instances[i].transform, transform, sizeof(transform));
            instances[i].instance_id     = i;
            instances[i].sbt_offset      = 0;
            instances[i].visibility_mask = 255;
        }

        /* Build IAS */
        auto build_start = std::chrono::high_resolution_clock::now();
        AccelManager ias_accel;
        AccelStructure ias = ias_accel.buildIAS(device.getContext(), instances);
        auto build_end     = std::chrono::high_resolution_clock::now();
        float build_ms     = std::chrono::duration<float, std::milli>(build_end - build_start).count();

        /* Compute overall bounding box */
        float half_span = (grid / 2.0f) * 3.0f + 1.5f;
        float3 bbox_min = make_float3(-half_span, -half_span, -1.5f);
        float3 bbox_max = make_float3( half_span,  half_span,  1.5f);

        /* Setup tracer and run benchmark */
        RayTracer tracer;
        tracer.init(ias_pipeline.getPipeline(), ias_pipeline.getSBT(), ias.handle);

        CudaBuffer<Ray>       d_rays;
        CudaBuffer<HitResult> d_hits;
        d_rays.alloc(fixed_ray_count);
        d_hits.alloc(fixed_ray_count);

        generateRandomRaysDevice(d_rays.get(), fixed_ray_count,
                                 bbox_min, bbox_max, 42);
        CUDA_CHECK(cudaDeviceSynchronize());

        TraceResult result = tracer.traceBatchTimed(
            d_rays.get(), d_hits.get(), fixed_ray_count, num_iters, 3);

        std::cout << std::right
                  << std::setw(12) << num_instances
                  << std::setw(14) << std::fixed << std::setprecision(3) << build_ms
                  << std::setw(14) << std::fixed << std::setprecision(3) << result.trace_time_ms
                  << std::setw(14) << std::fixed << std::setprecision(2) << result.mrays_per_sec
                  << std::setw(12) << std::fixed << std::setprecision(4) << result.grays_per_sec
                  << "\n";

        csv << "instancing," << num_instances << ","
            << build_ms << ","
            << result.trace_time_ms << ","
            << result.mrays_per_sec << ","
            << result.grays_per_sec << "\n";
    }
}

/* ======================================================================
 * Benchmark: Multi-Stream Concurrent Launch
 * ====================================================================*/
static void benchmarkMultiStream(
    RayTracer&   tracer,
    unsigned int total_rays,
    float3       bbox_min,
    float3       bbox_max,
    int          num_iters,
    std::ostream& csv)
{
    printSubHeader("Multi-Stream Concurrent Launch (" + formatNumber(total_rays) + " total rays)");

    std::vector<int> stream_counts = { 1, 2, 4, 8 };

    std::cout << std::right
              << std::setw(10) << "Streams"
              << std::setw(14) << "Rays/Stream"
              << std::setw(14) << "Total(ms)"
              << std::setw(14) << "MRays/s"
              << std::setw(12) << "GRays/s"
              << "\n"
              << std::string(64, '-') << "\n";

    for (int num_streams : stream_counts) {
        unsigned int rays_per_stream = total_rays / num_streams;

        /* Create streams and buffers */
        std::vector<cudaStream_t>        streams(num_streams);
        std::vector<CudaBuffer<Ray>>     ray_bufs(num_streams);
        std::vector<CudaBuffer<HitResult>> hit_bufs(num_streams);

        for (int s = 0; s < num_streams; ++s) {
            CUDA_CHECK(cudaStreamCreate(&streams[s]));
            ray_bufs[s].alloc(rays_per_stream);
            hit_bufs[s].alloc(rays_per_stream);
            generateRandomRaysDevice(ray_bufs[s].get(), rays_per_stream,
                                     bbox_min, bbox_max, 42 + s, streams[s]);
        }
        CUDA_CHECK(cudaDeviceSynchronize());

        /* Warm-up */
        for (int w = 0; w < 3; ++w) {
            for (int s = 0; s < num_streams; ++s) {
                tracer.traceBatch(ray_bufs[s].get(), hit_bufs[s].get(),
                                  rays_per_stream, streams[s]);
            }
            CUDA_CHECK(cudaDeviceSynchronize());
        }

        /* Timed runs */
        std::vector<float> times(num_iters);
        cudaEvent_t start, stop;
        CUDA_CHECK(cudaEventCreate(&start));
        CUDA_CHECK(cudaEventCreate(&stop));

        for (int i = 0; i < num_iters; ++i) {
            CUDA_CHECK(cudaEventRecord(start, 0));

            for (int s = 0; s < num_streams; ++s) {
                tracer.traceBatch(ray_bufs[s].get(), hit_bufs[s].get(),
                                  rays_per_stream, streams[s]);
            }

            CUDA_CHECK(cudaEventRecord(stop, 0));
            CUDA_CHECK(cudaEventSynchronize(stop));

            float ms = 0;
            CUDA_CHECK(cudaEventElapsedTime(&ms, start, stop));
            times[i] = ms;
        }

        CUDA_CHECK(cudaEventDestroy(start));
        CUDA_CHECK(cudaEventDestroy(stop));

        /* Cleanup streams */
        for (int s = 0; s < num_streams; ++s) {
            CUDA_CHECK(cudaStreamDestroy(streams[s]));
        }

        /* Results */
        std::sort(times.begin(), times.end());
        float median_ms = times[num_iters / 2];
        double mrays    = (double)total_rays / (median_ms * 1000.0);
        double grays    = mrays / 1000.0;

        std::cout << std::right
                  << std::setw(10) << num_streams
                  << std::setw(14) << formatNumber(rays_per_stream)
                  << std::setw(14) << std::fixed << std::setprecision(3) << median_ms
                  << std::setw(14) << std::fixed << std::setprecision(2) << mrays
                  << std::setw(12) << std::fixed << std::setprecision(4) << grays
                  << "\n";

        csv << "multistream," << num_streams << ","
            << rays_per_stream << ","
            << median_ms << ","
            << mrays << ","
            << grays << "\n";
    }
}

/* ======================================================================
 * MAIN
 * ====================================================================*/
int main(int argc, char* argv[])
{
    /* ---- Parse arguments ---- */
    bool         do_validate  = true;
    unsigned int max_rays     = 64 * 1024 * 1024;  /* 64M */
    int          num_iters    = 10;
    int          device_id    = 0;
    std::string  csv_file     = "optix_throughput_results.csv";

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--no-validate")          do_validate = false;
        else if (arg == "--max-rays" && i+1 < argc) max_rays  = std::stoul(argv[++i]);
        else if (arg == "--iters"    && i+1 < argc) num_iters = std::stoi(argv[++i]);
        else if (arg == "--device"   && i+1 < argc) device_id = std::stoi(argv[++i]);
        else if (arg == "--csv"      && i+1 < argc) csv_file  = argv[++i];
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  --no-validate      Skip validation tests\n"
                      << "  --max-rays <N>     Maximum ray count (default: 64M)\n"
                      << "  --iters <N>        Timed iterations (default: 10)\n"
                      << "  --device <id>      CUDA device (default: 0)\n"
                      << "  --csv <file>       CSV output file\n";
            return 0;
        }
    }

    try {
        /* ============================================================
         * 1. Device Initialization
         * ============================================================*/
        printHeader("OptiX 9.1.0 Throughput Benchmark");

        DeviceManager device;
        device.init(device_id, do_validate);
        device.printDeviceInfo();

        /* ============================================================
         * 2. Load PTX and Create Pipeline
         * ============================================================*/
        printSubHeader("Pipeline Setup");
        std::string ptx_source = findAndLoadPtx();
        std::cout << "  PTX loaded (" << ptx_source.size() << " bytes)\n";

        PipelineManager pipeline;
        pipeline.create(device.getContext(), ptx_source,
                        4,    /* payload registers */
                        2,    /* attribute registers (barycentrics) */
                        1,    /* max trace depth */
                        false /* no instancing for GAS-only tests */);

        /* ============================================================
         * 3. Build Test Scene (Cornell Box)
         * ============================================================*/
        printSubHeader("Scene Construction");

        TriangleMesh cornell = GeometryManager::createCornellBox();
        std::cout << "  Cornell Box: "
                  << cornell.indices.size() << " triangles, "
                  << cornell.vertices.size() << " vertices\n";

        AccelManager accel;
        AccelStructure gas = accel.buildGAS(device.getContext(), cornell, true);
        std::cout << "  GAS built: " << (gas.buffer_size / 1024) << " KB (compacted)\n";

        /* ============================================================
         * 4. Initialize Ray Tracer
         * ============================================================*/
        RayTracer tracer;
        tracer.init(pipeline.getPipeline(), pipeline.getSBT(), gas.handle);

        /* ============================================================
         * 5. Validation
         * ============================================================*/
        if (do_validate) {
            printHeader("Validation Tests");
            bool valid = runValidation(tracer);
            if (!valid) {
                std::cerr << "\n*** VALIDATION FAILED ***\n";
                return 1;
            }
            std::cout << "\n  All validation tests PASSED.\n";
        }

        /* ============================================================
         * 6. Throughput Benchmarks
         * ============================================================*/
        printHeader("Throughput Benchmarks");

        /* Open CSV output */
        std::ofstream csv(csv_file);
        csv << "test,param,build_ms,trace_ms,mrays_per_sec,grays_per_sec,hit_rate\n";

        /* 6a. Ray count sweep with Cornell Box */
        std::vector<unsigned int> ray_counts;
        for (unsigned int n = 1024; n <= max_rays; n *= 4) {
            ray_counts.push_back(n);
        }
        if (ray_counts.back() < max_rays) {
            ray_counts.push_back(max_rays);
        }

        benchmarkRayCountSweep(
            tracer, ray_counts,
            cornell.bbox_min, cornell.bbox_max,
            num_iters, "CornellBox", csv);

        /* 6b. Scene complexity sweep */
        benchmarkSceneComplexity(
            device, pipeline,
            1024 * 1024,  /* 1M rays */
            num_iters, csv);

        /* 6c. Instance acceleration test */
        benchmarkInstancing(
            device,
            1024 * 1024,  /* 1M rays */
            num_iters, csv);

        /* 6d. Multi-stream concurrent launch */
        benchmarkMultiStream(
            tracer,
            4 * 1024 * 1024,  /* 4M total rays */
            cornell.bbox_min, cornell.bbox_max,
            num_iters, csv);

        /* ============================================================
         * 7. Nearest Neighbor Query Tests
         * ============================================================*/
        printHeader("Nearest Neighbor Query (RT Core AABB)");

        printSubHeader("NN Pipeline Setup");
        std::string nn_ptx_source = findAndLoadNNPtx();
        std::cout << "  NN PTX loaded (" << nn_ptx_source.size() << " bytes)\n";

        NNQuery nn;
        nn.init(device.getContext(), nn_ptx_source);

        /* Create point cloud */
        const unsigned int nn_num_points = 100000;
        auto point_cloud = generateRandomPointCloud(nn_num_points, 10.0f, 42);

        /* Search radius: ~3x average inter-point distance for good coverage */
        float avg_spacing = 10.0f / std::pow((float)nn_num_points, 1.0f / 3.0f);
        float nn_radius   = avg_spacing * 3.0f;
        nn.setPointCloud(point_cloud, nn_radius);

        /* 7a. NN Validation */
        if (do_validate) {
            printSubHeader("NN Validation: Single & Batch Queries");

            /* Test 1: Query at a known point — should find itself (distance ~0) */
            NNResult nn_r = nn.querySingle(point_cloud[0]);
            bool nn_self_ok = (nn_r.distance >= 0.0f && nn_r.distance < 1e-5f
                               && nn_r.point_idx == 0);
            std::cout << "  Self-query: dist=" << nn_r.distance
                      << " idx=" << nn_r.point_idx
                      << (nn_self_ok ? " [PASS]" : " [FAIL]") << "\n";

            /* Test 2: Query at a known point offset — should find that point */
            float3 near_pt = make_float3(
                point_cloud[42].x + 0.001f,
                point_cloud[42].y,
                point_cloud[42].z);
            nn_r = nn.querySingle(near_pt);
            bool nn_near_ok = (nn_r.distance >= 0.0f && nn_r.distance < 0.01f);
            std::cout << "  Near-query: dist=" << nn_r.distance
                      << " idx=" << nn_r.point_idx
                      << (nn_near_ok ? " [PASS]" : " [FAIL]") << "\n";

            /* Test 3: Query far outside — should find nothing */
            float3 far_pt = make_float3(1000.0f, 1000.0f, 1000.0f);
            nn_r = nn.querySingle(far_pt);
            bool nn_far_ok = (nn_r.distance < 0.0f);
            std::cout << "  Far-query:  dist=" << nn_r.distance
                      << (nn_far_ok ? " [PASS]" : " [FAIL]") << "\n";

            /* Test 4: Batch query */
            std::vector<float3> test_queries(100);
            for (int i = 0; i < 100; ++i) {
                test_queries[i] = point_cloud[i * (nn_num_points / 100)];
            }
            auto nn_results = nn.queryBatch(test_queries);
            int nn_found = 0;
            for (const auto& r : nn_results) {
                if (r.distance >= 0.0f) nn_found++;
            }
            bool nn_batch_ok = (nn_found == 100);
            std::cout << "  Batch (100): " << nn_found << "/100 found"
                      << (nn_batch_ok ? " [PASS]" : " [FAIL]") << "\n";

            bool nn_valid = nn_self_ok && nn_near_ok && nn_far_ok && nn_batch_ok;
            if (!nn_valid) {
                std::cerr << "\n*** NN VALIDATION FAILED ***\n";
                return 1;
            }
            std::cout << "\n  All NN validation tests PASSED.\n";
        }

        /* 7b. NN Query Count Sweep */
        {
            printSubHeader("NN Query Count Sweep (" + formatNumber(nn_num_points)
                           + " points, radius=" + std::to_string(nn_radius).substr(0, 5) + ")");

            std::cout << std::right
                      << std::setw(12) << "Queries"
                      << std::setw(14) << "Time(ms)"
                      << std::setw(14) << "MQuery/s"
                      << std::setw(12) << "GQuery/s"
                      << std::setw(10) << "Found%"
                      << "\n"
                      << std::string(62, '-') << "\n";

            std::vector<unsigned int> nn_query_counts;
            for (unsigned int n = 1024; n <= max_rays; n *= 4) {
                nn_query_counts.push_back(n);
            }
            if (nn_query_counts.back() < max_rays) {
                nn_query_counts.push_back(max_rays);
            }

            for (unsigned int count : nn_query_counts) {
                CudaBuffer<float3>   d_queries;
                CudaBuffer<NNResult> d_results;
                d_queries.alloc(count);
                d_results.alloc(count);

                generateRandomQueriesDevice(d_queries.get(), count,
                                            nn.getBBoxMin(), nn.getBBoxMax(), 12345);
                CUDA_CHECK(cudaDeviceSynchronize());

                NNQueryResult qr = nn.queryBatchTimed(
                    d_queries.get(), d_results.get(), count, num_iters, 3);

                std::cout << std::right
                          << std::setw(12) << formatNumber(count)
                          << std::setw(14) << std::fixed << std::setprecision(3)
                          << qr.query_time_ms
                          << std::setw(14) << std::fixed << std::setprecision(2)
                          << qr.mqueries_per_sec
                          << std::setw(12) << std::fixed << std::setprecision(4)
                          << qr.gqueries_per_sec
                          << std::setw(9) << std::fixed << std::setprecision(1)
                          << (qr.found_rate * 100.0f) << "%"
                          << "\n";

                csv << "nn_sweep," << count << ","
                    << 0 << ","
                    << qr.query_time_ms << ","
                    << qr.mqueries_per_sec << ","
                    << qr.gqueries_per_sec << ","
                    << qr.found_rate << "\n";
            }
        }

        /* 7c. NN Point Count Sweep */
        {
            printSubHeader("NN Point Count Sweep (1M queries)");

            std::cout << std::right
                      << std::setw(12) << "Points"
                      << std::setw(14) << "Build(ms)"
                      << std::setw(14) << "Query(ms)"
                      << std::setw(14) << "MQuery/s"
                      << std::setw(12) << "GQuery/s"
                      << std::setw(10) << "Found%"
                      << "\n"
                      << std::string(76, '-') << "\n";

            std::vector<unsigned int> point_counts = {
                100, 1000, 10000, 100000, 500000, 1000000
            };
            const unsigned int fixed_nn_queries = 1024 * 1024;

            for (unsigned int pc : point_counts) {
                auto pts = generateRandomPointCloud(pc, 10.0f, 42);
                float radius = 10.0f / std::pow((float)pc, 1.0f / 3.0f) * 3.0f;

                NNQuery nn_bench;
                nn_bench.init(device.getContext(), nn_ptx_source);

                auto build_start = std::chrono::high_resolution_clock::now();
                nn_bench.setPointCloud(pts, radius);
                auto build_end = std::chrono::high_resolution_clock::now();
                float build_ms = std::chrono::duration<float, std::milli>(
                    build_end - build_start).count();

                CudaBuffer<float3>   d_queries;
                CudaBuffer<NNResult> d_results;
                d_queries.alloc(fixed_nn_queries);
                d_results.alloc(fixed_nn_queries);

                generateRandomQueriesDevice(d_queries.get(), fixed_nn_queries,
                                            nn_bench.getBBoxMin(),
                                            nn_bench.getBBoxMax(), 42);
                CUDA_CHECK(cudaDeviceSynchronize());

                NNQueryResult qr = nn_bench.queryBatchTimed(
                    d_queries.get(), d_results.get(), fixed_nn_queries,
                    num_iters, 3);

                std::cout << std::right
                          << std::setw(12) << formatNumber(pc)
                          << std::setw(14) << std::fixed << std::setprecision(3)
                          << build_ms
                          << std::setw(14) << std::fixed << std::setprecision(3)
                          << qr.query_time_ms
                          << std::setw(14) << std::fixed << std::setprecision(2)
                          << qr.mqueries_per_sec
                          << std::setw(12) << std::fixed << std::setprecision(4)
                          << qr.gqueries_per_sec
                          << std::setw(9) << std::fixed << std::setprecision(1)
                          << (qr.found_rate * 100.0f) << "%"
                          << "\n";

                csv << "nn_complexity," << pc << ","
                    << build_ms << ","
                    << qr.query_time_ms << ","
                    << qr.mqueries_per_sec << ","
                    << qr.gqueries_per_sec << ","
                    << qr.found_rate << "\n";

                nn_bench.cleanup();
            }
        }

        nn.cleanup();

        csv.close();

        /* ============================================================
         * 8. Summary
         * ============================================================*/
        printHeader("Benchmark Complete");
        std::cout << "  Results saved to: " << csv_file << "\n"
                  << "  Max ray count tested: " << formatNumber(max_rays) << "\n"
                  << "  Iterations per test: " << num_iters << "\n";

        /* ============================================================
         * 9. Cleanup
         * ============================================================*/
        printSubHeader("Cleanup");
        pipeline.cleanup();
        accel.cleanup();
        device.shutdown();
        std::cout << "  All resources released.\n";

    } catch (const std::exception& e) {
        std::cerr << "\n*** ERROR: " << e.what() << " ***\n";
        return 1;
    }

    return 0;
}
