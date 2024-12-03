import matplotlib.pyplot as plt
import numpy as np

font = {"size": 25}

plt.rc("font", **font)
# Data provided
data = np.array(
    [
        [1.8, 3.7, 7.8, 9.2],
        [1.5, 1.8, 1.4, 1.2],
        [1.8, 5.3, 11.7, 18.1],
        [1.8, 8.3, 21.1, 32.7],
        [2.5, 8.5, 37.6, 98.2],
        [3.6, 8.6, 29.1, 42.5],
        [2.6, 7.1, 14.6, 17.0],
        [7.3, 23.5, 57.7, 83.3],
        [8.9, 20.0, 35.4, 39.9],
        [3.3, 8.6, 15.6, 19.9],
        [7.4, 13.5, 22.9, 22.6],
    ]
)

data2 = np.array(
    [
        [0.9, 0.7, 0.6, 0.8],
        [1.4, 1.7, 0.7, 0.7],
        [1.1, 1.1, 0.9, 1.0],
        [0.9, 1.2, 1.1, 1.7],
        [0.9, 1.2, 0.9, 1.5],
        [0.9, 1.0, 0.8, 1.3],
        [1.2, 1.4, 0.8, 0.8],
        [2.3, 2.8, 1.9, 1.7],
        [3.0, 3.0, 1.6, 1.1],
        [4.6, 13.0, 14.4, 13.5],
        [1.2, 0.9, 0.5, 0.3],
    ]
)
labels = ["bc", "bfs", "cc", "cc_sv", "pr", "pr_spmv", "sssp", "bt", "cg", "ft", "mg"]

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(20, 8))

# Plotting each row as a line on the first graph
for i in range(data.shape[0]):
    ax1.plot(data[i], label=labels[i])

# Adding labels and title to the first graph
ax1.set_xlabel("Performance 5418Y over M3Max E core")
ax1.set_ylabel("Threads")
ax1.legend(fontsize="small")
# ax1.set_title("Line plots of provided data (Left)")
ax1.set_xticks([0, 1, 2, 3])
ax1.set_xticklabels(["4", "16", "64", "96"])

# Plotting each row as a line on the second graph
for i in range(data2.shape[0]):
    ax2.plot(data2[i], label=labels[i])
ax2.axhline(y=1.7, color='r', linestyle='--', linewidth=1)
# Adding labels and title to the second graph
ax2.set_xlabel("Performance 5418Y over Epyc")
ax2.set_ylabel("Threads")
# ax2.set_title("Line plots of provided data (Right)")
ax2.set_xticks([0, 1, 2, 3])
ax2.set_xticklabels(["4", "16", "64", "96"])
ax2.legend(fontsize="small")

plt.savefig("cyberforaging.pdf")
