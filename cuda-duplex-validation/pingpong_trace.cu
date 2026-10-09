#include <cuda_runtime.h>
#include <stdio.h>
#include <thread>
#include <chrono>

const int N = 1<<26;

__global__ void kernel_test(float* d)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;

    if(i < N)
    {
        float v = d[i];

        for(int k=0;k<2000;k++)
            v = v * 1.000001f + 0.000001f;

        d[i] = v;
    }
}

struct Task
{
    float* h;
    float* d;

    cudaStream_t stream;
    cudaEvent_t done;
};

void cpu_compute(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void submit(Task& t)
{
    cudaMemcpyAsync(
        t.d,
        t.h,
        N*sizeof(float),
        cudaMemcpyHostToDevice,
        t.stream
    );

    kernel_test<<<(N+255)/256,256,0,t.stream>>>(t.d);

    cudaMemcpyAsync(
        t.h,
        t.d,
        N*sizeof(float),
        cudaMemcpyDeviceToHost,
        t.stream
    );

    cudaEventRecord(t.done,t.stream);
}
#define CPUTIME 15
int main()
{
    Task A,B;

    cudaMallocHost(&A.h,N*sizeof(float));
    cudaMallocHost(&B.h,N*sizeof(float));

    cudaMalloc(&A.d,N*sizeof(float));
    cudaMalloc(&B.d,N*sizeof(float));

    cudaStreamCreate(&A.stream);
    cudaStreamCreate(&B.stream);

    cudaEventCreate(&A.done);
    cudaEventCreate(&B.done);

    int iterations = 6;

    // warmup
    cpu_compute(CPUTIME);
    submit(A);

    cpu_compute(CPUTIME);
    submit(B);

    for(int i=0;i<iterations;i++)
    {
        cudaEventSynchronize(A.done);

        cpu_compute(CPUTIME);
        submit(A);

        cudaEventSynchronize(B.done);

        cpu_compute(CPUTIME);
        submit(B);
    }

    cudaDeviceSynchronize();

    printf("done\n");
}