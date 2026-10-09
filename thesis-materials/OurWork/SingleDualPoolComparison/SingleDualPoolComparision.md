# Comparision between Single and Dual Pool

本实验在 porous 场景下固定Thread = 32, PoolSize = 8192, 比较单池和双池两种实现的性能差异。结果如下。

|config|total_time/step|refill/step|drain/step|
|-|-|-|-|
|Dual Pool|104s/482079|89s/386884|15s/95295|
|Single Pool|169s/467339|157s/394330|12s/73009|