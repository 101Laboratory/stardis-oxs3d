# Wavefront 求解器OMP多线程加速基准测试结果

**创建日期**: 2026-02-19  
**状态**: 已完成【所有数据现已过时】
**测试场景**: porous 场景，256x256 分辨率，spp=4

# 1. 测试结果

## Take1 - omp off, pool_size=4096

```plaintext
message: persistent_wavefront DONE: 256x256 spp=4 pool=4096 elapsed=4 mins 47 secs 84 msecs 651 usecs 400 nsecs  steps=123711  rays=961564762  avg_width=1970.5  (rad=267169 cond_ds=185115278 ds_retry=613120)  done: rad=216332 temp=445 bnd=44903 fail=464  max_depth=297954
message: persistent wavefront summary:
  total_steps=123711  total_rays=961564762  avg_wavefront_width=1970.5
  refill_phase: rays=854696362 (88.9%)  wall=233.497s
  drain_phase:  rays=106868400 (11.1%)  steps=70804  wall=53.588s
  batch_size: min=2, max=17558
  ray_buckets: radiative=267169, step_pair=185115278, shadow=0, startup=0
  paths: completed=262144, failed=464, truncated=0, max_depth=297954
  refills=258048
  timing: compact=6.119s  collect=12.367s  trace=150.882s  distribute=18.319s  cascade=83.228s  harvest+refill=8.958s
message:   enc_escalation: query_fb->m10=221  m10_degenerate_null=221
message: cascade profiling: total_iterations=400366978  total_advances=400366783
message:   cascade phase[34]: count=  92864199  time=  16.470s  avg=0.177us  (19.8% of cascade)
message:   cascade phase[38]: count=  92821008  time=  15.694s  avg=0.169us  (18.9% of cascade)
message:   cascade phase[16]: count=  64044264  time=  10.471s  avg=0.163us  (12.6% of cascade)
message:   cascade phase[ 3]: count=  32065730  time=   9.127s  avg=0.285us  (11.0% of cascade)
message:   cascade phase[37]: count=  67132371  time=   7.245s  avg=0.108us  (8.7% of cascade)
message:   cascade phase[ 5]: count=  25688906  time=   2.780s  avg=0.108us  (3.3% of cascade)
message:   cascade phase[15]: count=  25746719  time=   1.706s  avg=0.066us  (2.0% of cascade)
message:   cascade phase[18]: count=      2995  time=   0.001s  avg=0.355us  (0.0% of cascade)
message:   cascade phase[10]: count=       505  time=   0.000s  avg=0.052us  (0.0% of cascade)
message:   cascade phase[ 9]: count=        60  time=   0.000s  avg=0.352us  (0.0% of cascade)
message: batch trace profiling: calls=123711  avg_batch=7773  min=2  max=17558
  gpu_kernel+upload: total=135850.7ms (90.2%)  avg=1.10ms/call
  cpu_postprocess:   total=14047.1ms (9.3%)  avg=0.11ms/call
  fallback_retrace:  total=642.9ms (0.4%)  accepted=11  missed=13  rejected=56301
  gpu_throughput: 7.1 Mrays/s  (kernel+upload only)
message: Initialisation time = 218 msecs
message: Computation time = 4 mins 47 secs 114 msecs
```

## Take2 - omp on, pool_size=4096

```plaintext
message: persistent_wavefront DONE: 256x256 spp=4 pool=4096 elapsed=2 mins 32 secs 155 msecs 803 usecs 900 nsecs  steps=123711  rays=961564762  avg_width=1970.5  (rad=267169 cond_ds=185115278 ds_retry=613120)  done: rad=216332 temp=445 bnd=44903 fail=464  max_depth=297954
message: persistent wavefront summary:
  total_steps=123711  total_rays=961564762  avg_wavefront_width=1970.5
  refill_phase: rays=854696362 (88.9%)  wall=110.258s
  drain_phase:  rays=106868400 (11.1%)  steps=70804  wall=41.898s
  batch_size: min=2, max=17558
  ray_buckets: radiative=267169, step_pair=185115278, shadow=0, startup=0
  paths: completed=262144, failed=464, truncated=0, max_depth=297954
  refills=258048
  timing: compact=5.534s  collect=14.325s  trace=89.581s  distribute=16.758s  cascade=10.990s  harvest+refill=8.644s
message:   enc_escalation: query_fb->m10=221  m10_degenerate_null=221
message: cascade profiling: total_iterations=400366978  total_advances=400366783
message:   cascade phase[34]: count=  92864199  time=  18.352s  avg=0.198us  (167.0% of cascade)
message:   cascade phase[38]: count=  92821008  time=  18.322s  avg=0.197us  (166.7% of cascade)
message:   cascade phase[16]: count=  64044264  time=  10.955s  avg=0.171us  (99.7% of cascade)
message:   cascade phase[ 3]: count=  32065730  time=   9.152s  avg=0.285us  (83.3% of cascade)
message:   cascade phase[37]: count=  67132371  time=   7.606s  avg=0.113us  (69.2% of cascade)
message:   cascade phase[ 5]: count=  25688906  time=   2.792s  avg=0.109us  (25.4% of cascade)
message:   cascade phase[15]: count=  25746719  time=   1.834s  avg=0.071us  (16.7% of cascade)
message:   cascade phase[18]: count=      2995  time=   0.001s  avg=0.352us  (0.0% of cascade)
message:   cascade phase[10]: count=       505  time=   0.000s  avg=0.049us  (0.0% of cascade)
message:   cascade phase[ 9]: count=        60  time=   0.000s  avg=0.318us  (0.0% of cascade)
message: batch trace profiling: calls=123711  avg_batch=7773  min=2  max=17558
  gpu_kernel+upload: total=76893.1ms (86.1%)  avg=0.62ms/call
  cpu_postprocess:   total=11929.5ms (13.4%)  avg=0.10ms/call
  fallback_retrace:  total=494.2ms (0.6%)  accepted=11  missed=13  rejected=56301
  gpu_throughput: 12.5 Mrays/s  (kernel+upload only)
message: Initialisation time = 286 msecs
message: Computation time = 2 mins 32 secs 281 msecs
```

mode,threads,run,wall_clock_s
serial,1,1,362.325
omp,32,1,239.936

## Take3 - omp off, pool_size=16384

```plaintext
message: persistent_wavefront DONE: 256x256 spp=4 pool=16384 elapsed=6 mins 1 sec 903 msecs 333 usecs 200 nsecs  steps=112222  rays=961564762  avg_width=2172.2  (rad=267169 cond_ds=185115278 ds_retry=613120)  done: rad=216332 temp=445 bnd=44903 fail=464  max_depth=297954
message: persistent wavefront summary:
  total_steps=112222  total_rays=961564762  avg_wavefront_width=2172.2
  refill_phase: rays=486238447 (50.6%)  wall=129.940s
  drain_phase:  rays=475326315 (49.4%)  steps=104693  wall=231.964s
  batch_size: min=1, max=69080
  ray_buckets: radiative=267169, step_pair=185115278, shadow=0, startup=0
  paths: completed=262144, failed=464, truncated=0, max_depth=297954
  refills=245760
  timing: compact=19.947s  collect=19.284s  trace=128.132s  distribute=27.740s  cascade=110.178s  harvest+refill=36.427s
message:   enc_escalation: query_fb->m10=221  m10_degenerate_null=221
message: cascade profiling: total_iterations=400366978  total_advances=400366783
message:   cascade phase[38]: count=  92821008  time=  27.195s  avg=0.293us  (24.7% of cascade)
message:   cascade phase[34]: count=  92864199  time=  20.540s  avg=0.221us  (18.6% of cascade)
message:   cascade phase[16]: count=  64044264  time=  14.452s  avg=0.226us  (13.1% of cascade)
message:   cascade phase[ 3]: count=  32065730  time=   9.944s  avg=0.310us  (9.0% of cascade)
message:   cascade phase[37]: count=  67132371  time=   9.039s  avg=0.135us  (8.2% of cascade)
message:   cascade phase[15]: count=  25746719  time=   3.183s  avg=0.124us  (2.9% of cascade)
message:   cascade phase[ 5]: count=  25688906  time=   2.856s  avg=0.111us  (2.6% of cascade)
message:   cascade phase[18]: count=      2995  time=   0.001s  avg=0.466us  (0.0% of cascade)
message:   cascade phase[10]: count=       505  time=   0.000s  avg=0.054us  (0.0% of cascade)
message:   cascade phase[ 9]: count=        60  time=   0.000s  avg=0.352us  (0.0% of cascade)
message: batch trace profiling: calls=112222  avg_batch=8568  min=1  max=69080
  gpu_kernel+upload: total=110164.8ms (86.2%)  avg=0.98ms/call
  cpu_postprocess:   total=16943.2ms (13.3%)  avg=0.15ms/call
  fallback_retrace:  total=662.6ms (0.5%)  accepted=11  missed=13  rejected=56301
  gpu_throughput: 8.7 Mrays/s  (kernel+upload only)
message: Initialisation time = 231 msecs
message: Computation time = 6 mins 1 sec 947 msecs
```

## Take4 - omp on, pool_size=16384

```plaintext
message: persistent_wavefront DONE: 256x256 spp=4 pool=16384 elapsed=3 mins 59 secs 437 msecs 371 usecs 900 nsecs  steps=112222  rays=961564762  avg_width=2172.2  (rad=267169 cond_ds=185115278 ds_retry=613120)  done: rad=216332 temp=445 bnd=44903 fail=464  max_depth=297954
message: persistent wavefront summary:
  total_steps=112222  total_rays=961564762  avg_wavefront_width=2172.2
  refill_phase: rays=486238447 (50.6%)  wall=65.519s
  drain_phase:  rays=475326315 (49.4%)  steps=104693  wall=173.918s
  batch_size: min=1, max=69080
  ray_buckets: radiative=267169, step_pair=185115278, shadow=0, startup=0
  paths: completed=262144, failed=464, truncated=0, max_depth=297954
  refills=245760
  timing: compact=18.664s  collect=17.146s  trace=115.529s  distribute=23.969s  cascade=10.897s  harvest+refill=34.479s
message:   enc_escalation: query_fb->m10=221  m10_degenerate_null=221
message: cascade profiling: total_iterations=400366978  total_advances=400366783
message:   cascade phase[38]: count=  92821008  time=  30.379s  avg=0.327us  (278.8% of cascade)
message:   cascade phase[34]: count=  92864199  time=  22.200s  avg=0.239us  (203.7% of cascade)
message:   cascade phase[16]: count=  64044264  time=  15.806s  avg=0.247us  (145.1% of cascade)
message:   cascade phase[ 3]: count=  32065730  time=   9.779s  avg=0.305us  (89.7% of cascade)
message:   cascade phase[37]: count=  67132371  time=   8.356s  avg=0.124us  (76.7% of cascade)
message:   cascade phase[15]: count=  25746719  time=   3.321s  avg=0.129us  (30.5% of cascade)
message:   cascade phase[ 5]: count=  25688906  time=   2.619s  avg=0.102us  (24.0% of cascade)
message:   cascade phase[18]: count=      2995  time=   0.001s  avg=0.481us  (0.0% of cascade)
message:   cascade phase[10]: count=       505  time=   0.000s  avg=0.049us  (0.0% of cascade)
message:   cascade phase[51]: count=       221  time=   0.000s  avg=0.105us  (0.0% of cascade)
message: batch trace profiling: calls=112222  avg_batch=8568  min=1  max=69080
  gpu_kernel+upload: total=101358.6ms (87.9%)  avg=0.90ms/call
  cpu_postprocess:   total=13381.0ms (11.6%)  avg=0.12ms/call
  fallback_retrace:  total=514.6ms (0.4%)  accepted=11  missed=13  rejected=56301
  gpu_throughput: 9.5 Mrays/s  (kernel+upload only)
message: Initialisation time = 247 msecs
message: Computation time = 3 mins 59 secs 556 msecs
```

```csv
mode,threads,run,wall_clock_s
serial,1,1,287.469
omp,32,1,152.696
```