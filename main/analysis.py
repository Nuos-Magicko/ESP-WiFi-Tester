import pandas as pd
import os

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CSV_PATH = os.path.join(BASE_DIR, "mgi_stats_samples.csv")
SAMPLE= 30

df = pd.read_csv(CSV_PATH)

groups = df.head(SAMPLE)

stats = groups.agg({
    "rssi": ["mean","median","min","max","std"],
    "throughput_mbps": ["mean","median","min","max","std"]
})

stats.to_csv(os.path.join(BASE_DIR, "mgi_stats_30samples.csv"))
