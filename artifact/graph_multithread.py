import matplotlib.pyplot as plt
import numpy as np

# Your data
items = ["bt", "cg", "ep", "ft", "lu", "mg", "sp", "linpack", "llama"]
hcontainer_values = [261.77, 111.80, 0.0035, 205.29, 29.10, 62.92, 0.28, 27.15, 12.00]
mvvm_values = [85.05, 27.64, 0.000179, 39.12, 8.83, 18.80, 0.118, 35.48, 3.54]
native_values = [46.84, 27.88, 0.00, 28.96, 8.56, 9.34, 0.07, 35.0, 2.86]
qemu_values = [1936.74, 473.24, 0.02, 1376.74, 373.91, 646.23, 2.75, 24.48, 45.18]

# Number of groups and bar width
n = len(items)
bar_width = 0.2

# Setting the positions of the bars on the x-axis
index = np.arange(n)

# Creating figure
fig, ax = plt.subplots(figsize=(12, 6))

# Plotting
bar1 = ax.bar(index, hcontainer_values, bar_width, label='HContainer')
bar2 = ax.bar(index + bar_width, mvvm_values, bar_width, label='MVVM')
bar3 = ax.bar(index + 2 * bar_width, native_values, bar_width, label='Native')
bar4 = ax.bar(index + 3 * bar_width, qemu_values, bar_width, label='QEMU')

# Adding labels, title, and legend
ax.set_xlabel('Items')
ax.set_ylabel('Values')
ax.set_title('Performance comparison between different techniques')
ax.set_xticks(index + bar_width * 1.5)
ax.set_xticklabels(items)
ax.legend()

# Display the plot
plt.show()