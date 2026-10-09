#include <cuda_runtime.h>
#include <stdio.h>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

const int N = 1<<20;

__global__ void test_kernel(float* data)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N)
        data[i] = data[i] * 2.0f + 1.0f;
}

struct Stamp
{
    long long us;
    std::string obj;
    std::string stage;
};

std::vector<Stamp> timeline;

auto T0 = std::chrono::high_resolution_clock::now();

long long now_us()
{
    auto t = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(t - T0).count();
}

void log_cpu(const char* obj,const char* stage)
{
    timeline.push_back({now_us(),obj,stage});
}

struct Task
{
    float* h;
    float* d;
    cudaStream_t stream;

    cudaEvent_t h2d_begin;
    cudaEvent_t h2d_end;

    cudaEvent_t k_begin;
    cudaEvent_t k_end;

    cudaEvent_t d2h_begin;
    cudaEvent_t d2h_end;
};

void cpu_compute_sleep(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void launch(Task& t)
{
    cudaEventRecord(t.h2d_begin,t.stream);

    cudaMemcpyAsync(
        t.d,
        t.h,
        N*sizeof(float),
        cudaMemcpyHostToDevice,
        t.stream
    );

    cudaEventRecord(t.h2d_end,t.stream);

    cudaEventRecord(t.k_begin,t.stream);

    test_kernel<<<(N+255)/256,256,0,t.stream>>>(t.d);

    cudaEventRecord(t.k_end,t.stream);

    cudaEventRecord(t.d2h_begin,t.stream);

    cudaMemcpyAsync(
        t.h,
        t.d,
        N*sizeof(float),
        cudaMemcpyDeviceToHost,
        t.stream
    );

    cudaEventRecord(t.d2h_end,t.stream);
}

void record_gpu(Task& t,const char* name)
{
    float ms;

    cudaEventElapsedTime(&ms,t.h2d_begin,t.h2d_end);
    timeline.push_back({(long long)(ms*1000),name,"H2D"});

    cudaEventElapsedTime(&ms,t.k_begin,t.k_end);
    timeline.push_back({(long long)(ms*1000),name,"KERNEL"});

    cudaEventElapsedTime(&ms,t.d2h_begin,t.d2h_end);
    timeline.push_back({(long long)(ms*1000),name,"D2H"});
}

int main()
{
    Task A,B;

    cudaMallocHost(&A.h,N*sizeof(float));
    cudaMallocHost(&B.h,N*sizeof(float));

    cudaMalloc(&A.d,N*sizeof(float));
    cudaMalloc(&B.d,N*sizeof(float));

    cudaStreamCreate(&A.stream);
    cudaStreamCreate(&B.stream);

    cudaEventCreate(&A.h2d_begin);
    cudaEventCreate(&A.h2d_end);
    cudaEventCreate(&A.k_begin);
    cudaEventCreate(&A.k_end);
    cudaEventCreate(&A.d2h_begin);
    cudaEventCreate(&A.d2h_end);

    cudaEventCreate(&B.h2d_begin);
    cudaEventCreate(&B.h2d_end);
    cudaEventCreate(&B.k_begin);
    cudaEventCreate(&B.k_end);
    cudaEventCreate(&B.d2h_begin);
    cudaEventCreate(&B.d2h_end);

    int cpuA_ms = 5;
    int cpuB_ms = 8;

    int iterations = 10;

    for(int i=0;i<iterations;i++)
    {
        log_cpu("A","CPU_BEGIN");

        cpu_compute_sleep(cpuA_ms);

        log_cpu("A","CPU_END");

        launch(A);

        log_cpu("B","CPU_BEGIN");

        cpu_compute_sleep(cpuB_ms);

        log_cpu("B","CPU_END");

        launch(B);
    }

    cudaDeviceSynchronize();

    record_gpu(A,"A");
    record_gpu(B,"B");

    printf("timestamp_us,object,stage\n");

    for(auto& s:timeline)
    {
        printf("%lld,%s,%s\n",
            s.us,
            s.obj.c_str(),
            s.stage.c_str());
    }
}