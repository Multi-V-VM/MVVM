# %%

import matplotlib.pyplot as plt
import numpy as np

# Given data
result = [
    ("a=b linpack.aot", 4.147552, 2.550483),
    ("a=b linpack.aot", 4.164721, 2.58253),
    ("a=b llama.aot stories15M.bin -z tokenizer.bin -t 0.0", 0.238909, 2.58253),
    ("a=b llama.aot stories15M.bin -z tokenizer.bin -t 0.0", 0.238602, 2.58253),
]
# Split the data by workload
linpack_data = [entry[1:] for entry in result if "linpack" in entry[0]]
llama_data = [entry[1:] for entry in result if "llama" in entry[0]]

# Calculate the median and standard deviation for both workloads
linpack_snapshot_data = [entry[0] for entry in linpack_data]
linpack_recover_data = [entry[1] for entry in linpack_data]
llama_snapshot_data = [entry[0] for entry in llama_data]
llama_recover_data = [entry[1] for entry in llama_data]


# Function to calculate medians and IQRs
def calc_median_iqr(data):
    median = np.median(data)
    iqr = np.percentile(data, 75) - np.percentile(data, 25)
    return median, iqr


# Calculate medians and IQRs
linpack_snapshot_median, linpack_snapshot_iqr = calc_median_iqr(linpack_snapshot_data)
linpack_recover_median, linpack_recover_iqr = calc_median_iqr(linpack_recover_data)
llama_snapshot_median, llama_snapshot_iqr = calc_median_iqr(llama_snapshot_data)
llama_recover_median, llama_recover_iqr = calc_median_iqr(llama_recover_data)

# Plotting
x = np.arange(2)  # the label locations
width = 0.35  # the width of the bars

fig, ax = plt.subplots()
rects1 = ax.bar(
    x - width / 2,
    [linpack_snapshot_median, linpack_recover_median],
    width,
    yerr=[linpack_snapshot_iqr / 2, linpack_recover_iqr / 2],
    label="Linpack",
    capsize=5,
)
rects2 = ax.bar(
    x + width / 2,
    [llama_snapshot_median, llama_recover_median],
    width,
    yerr=[llama_snapshot_iqr / 2, llama_recover_iqr / 2],
    label="Llama",
    capsize=5,
)

# Add some text for labels, title and custom x-axis tick labels
ax.set_ylabel("Time")
ax.set_title("Median Snapshot and Recovery Times by Workload")
ax.set_xticks(x)
ax.set_xticklabels(["Snapshot", "Recovery"])
ax.legend()


def autolabel(rects):
    """Attach a text label above each bar in *rects*, displaying its height."""
    for rect in rects:
        height = rect.get_height()
        ax.annotate(
            f"{height:.2f}",
            xy=(rect.get_x() + rect.get_width() / 2, height),
            xytext=(0, 3),  # 3 points vertical offset
            textcoords="offset points",
            ha="center",
            va="bottom",
        )


autolabel(rects1)
autolabel(rects2)

fig.tight_layout()
plt.show()
