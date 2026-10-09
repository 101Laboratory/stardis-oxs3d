# Generate a Gantt chart where:
# - CPU tasks share one color and one row
# - GPU activities are split into three rows (H2D, Kernel, D2H) with different colors

import matplotlib.pyplot as plt

cpu = [
    (-0.291, 0.000),
    (0.131, 0.471),
    (0.530, 0.852),
    (0.904, 1.203),
    (1.278, 1.603),
    (1.661, 1.967),
]

h2d = [
    (-0.349, -0.205),
    (0.049, 0.140),
    (0.718, 0.831),
    (1.117, 1.233),
    (1.538, 1.649),
    (2.010, 2.122),
]

kern = [
    (-0.174, -0.120),
    (0.280, 0.486),
    (0.861, 0.917),
    (1.266, 1.322),
    (1.681, 1.738),
    (2.153, 2.210),
]

d2h = [
    (-0.099, 0.000),
    (0.526, 0.615),
    (0.936, 1.034),
    (1.346, 1.452),
    (1.764, 1.863),
    (2.231, 2.332),
]

rows = ["CPU", "GPU_H2D", "GPU_Kernel", "GPU_D2H"]
ypos = [3, 2, 1, 0]

plt.figure()
plt.figure(figsize=(14, 2))
plt.grid()

for s, e in cpu:
    plt.barh(ypos[0], e - s, left=s, color="tab:blue")

for s, e in h2d:
    plt.barh(ypos[1], e - s, left=s + 0.349, color="tab:orange")

for s, e in kern:
    plt.barh(ypos[1], e - s, left=s + 0.349, color="tab:green")

for s, e in d2h:
    plt.barh(ypos[1], e - s, left=s + 0.349, color="tab:red")

plt.yticks(ypos, rows)
plt.xlabel("Time (ms)")
plt.title("CPU/GPU Pipeline Gantt")
plt.tight_layout()
plt.show()