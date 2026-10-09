import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("timeline.csv")

pairs = []

stack = {}

for _,r in df.iterrows():

    if r.stage.endswith("BEGIN"):
        stack[(r.object,r.stage.replace("_BEGIN",""))] = r.timestamp_us

    if r.stage.endswith("END"):
        key=(r.object,r.stage.replace("_END",""))
        if key in stack:
            start = stack[key]
            pairs.append({
                "obj":r.object,
                "stage":key[1],
                "start":start,
                "duration":r.timestamp_us-start
            })

g = pd.DataFrame(pairs)

fig,ax=plt.subplots(figsize=(10,4))

y_map={"CPU":0,"H2D":1,"KERNEL":2,"D2H":3}

for _,r in g.iterrows():
    y=y_map[r.stage]
    ax.barh(y,r.duration,left=r.start)

ax.set_yticks(list(y_map.values()))
ax.set_yticklabels(list(y_map.keys()))

ax.set_xlabel("time (us)")
plt.show()