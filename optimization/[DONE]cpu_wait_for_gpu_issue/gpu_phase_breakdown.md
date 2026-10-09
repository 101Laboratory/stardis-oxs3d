# CPU Wait-for-GPU 瓶颈诊断 - GPU Phase Breakdown实验数据

**创建日期**: 2026-03-04

# 采集内容：

[TODO]

# 数据

## pool_size = 8192 (半池宽度)

message:   gpu-wait sub-phase breakdown (A4):
    aos2soa_upload: total=  7612.1ms  avg=0.042ms/call  (10.3%)
    cuda_sync_wait: total=  5029.2ms  avg=0.027ms/call  (6.8%)
    d2h_download:   total= 33653.4ms  avg=0.183ms/call  (45.7%)
    cpu_filter_eval:total=  7800.1ms  avg=0.043ms/call  (10.6%)
    cpu_postprocess:total=  7800.1ms  avg=0.043ms/call  (10.6%)
    fallback_rtrc:  total= 11801.0ms  avg=0.064ms/call  (16.0%)

## pool_size = 12244 (半池宽度)

message:   gpu-wait sub-phase breakdown (A4):
    aos2soa_upload: total=  8827.9ms  avg=0.059ms/call  (11.7%)
    cuda_sync_wait: total=  5825.6ms  avg=0.039ms/call  (7.7%)
    d2h_download:   total= 33783.0ms  avg=0.224ms/call  (44.7%)
    cpu_filter_eval:total=  8719.7ms  avg=0.058ms/call  (11.5%)
    cpu_postprocess:total=  8719.7ms  avg=0.058ms/call  (11.5%)
    fallback_rtrc:  total=  9781.4ms  avg=0.065ms/call  (12.9%)

## pool_size = 16384 (半池宽度)

message:   gpu-wait sub-phase breakdown (A4):
    aos2soa_upload: total=  9290.0ms  avg=0.068ms/call  (11.7%)
    cuda_sync_wait: total=  6932.6ms  avg=0.051ms/call  (8.7%)
    d2h_download:   total= 33355.1ms  avg=0.243ms/call  (41.9%)
    cpu_filter_eval:total= 10361.0ms  avg=0.075ms/call  (13.0%)
    cpu_postprocess:total= 10361.0ms  avg=0.075ms/call  (13.0%)
    fallback_rtrc:  total=  9389.4ms  avg=0.068ms/call  (11.8%)

## pool_size = 32768 (半池宽度)

message:   gpu-wait sub-phase breakdown (A4):
    aos2soa_upload: total= 11009.0ms  avg=0.093ms/call  (11.9%)
    cuda_sync_wait: total=  8882.7ms  avg=0.075ms/call  (9.6%)
    d2h_download:   total= 39874.6ms  avg=0.337ms/call  (43.0%)
    cpu_filter_eval:total= 12406.7ms  avg=0.105ms/call  (13.4%)
    cpu_postprocess:total= 12406.7ms  avg=0.105ms/call  (13.4%)
    fallback_rtrc:  total=  8142.7ms  avg=0.069ms/call  (8.8%)