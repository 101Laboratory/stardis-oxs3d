#include <cuda_runtime.h>
#include <stdio.h>

int main() {
    const size_t size = 256 * 1024 * 1024;

    char *hA, *hB, *dA, *dB;

    cudaMallocHost(&hA, size);
    cudaMallocHost(&hB, size);
    cudaMalloc(&dA, size);
    cudaMalloc(&dB, size);

    cudaStream_t s1, s2;
    cudaStreamCreate(&s1);
    cudaStreamCreate(&s2);

    cudaEvent_t h2d_start, h2d_end;
    cudaEvent_t d2h_start, d2h_end;

    cudaEventCreate(&h2d_start);
    cudaEventCreate(&h2d_end);
    cudaEventCreate(&d2h_start);
    cudaEventCreate(&d2h_end);

    cudaEventRecord(h2d_start, s1);
    cudaMemcpyAsync(dA, hA, size, cudaMemcpyHostToDevice, s1);
    cudaEventRecord(h2d_end, s1);

    cudaEventRecord(d2h_start, s2);
    cudaMemcpyAsync(hB, dB, size, cudaMemcpyDeviceToHost, s2);
    cudaEventRecord(d2h_end, s2);

    cudaDeviceSynchronize();

    float h2d_time, d2h_time;
    float overlap_test;

    cudaEventElapsedTime(&h2d_time, h2d_start, h2d_end);
    cudaEventElapsedTime(&d2h_time, d2h_start, d2h_end);
    cudaEventElapsedTime(&overlap_test, h2d_start, d2h_end);

    printf("H2D time: %f ms\n", h2d_time);
    printf("D2H time: %f ms\n", d2h_time);
    printf("Total span: %f ms\n", overlap_test);

    if (overlap_test < (h2d_time + d2h_time))
        printf("OVERLAP DETECTED (full duplex)\n");
    else
        printf("NO OVERLAP\n");
}