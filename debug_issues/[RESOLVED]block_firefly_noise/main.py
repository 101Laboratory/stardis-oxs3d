import pandas as pd
import numpy as np

gpu = pd.read_csv("gpu_trace.csv")
# 按行号≈harvest顺序，加一个近似task_id
# gpu['row_id'] = range(len(gpu))

# 分 bin（每 pool_size 行一个 bin ≈ 一轮 fill/refill）
POOL = 4096
gpu['batch'] = gpu['path_id'] // POOL

# 统计每个 batch 的温度分布
stats = gpu.groupby('batch')['T_value'].agg(['mean', 'std', 'min', 'max', 'count'])
print(stats)

# 找异常：mean 偏移或 std 爆炸的 batch

