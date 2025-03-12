import matplotlib.pyplot as plt
import numpy as np


def plot_graph(results, labels):
    font = {'size': 40}
    plt.rc('font', **font)
    # Define colors for each platform
    colors = {
        'MVVM-Vanilla': 'cyan',
        'MVVM-GPU': 'red',
        'SGLang-GPU': 'blue',
    }
    
    # Create figure and axis
    fig, ax = plt.subplots(figsize=(10, 10))
    
    # Create bar positions
    x = np.arange(len(results))
    
    # Create bars with specified colors
    bars = ax.bar(x, results, width=0.3)
    
    # Color each bar according to the platform
    for bar, label in zip(bars, labels):
        bar.set_color(colors.get(label, 'gray'))  # Default to gray if color not found
    
    # Customize the plot
    ax.set_ylabel('Latency (s)')
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=45, fontsize=30)
    
    # Add value labels on top of each bar
    for bar in bars:
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2., height,
                f'{height:.4f}',
                ha='center', va='bottom')
    
    # Add grid for better readability
    ax.grid(True, axis='y', linestyle='--', alpha=0.7)
    
    # Adjust layout to prevent label cutoff
    plt.tight_layout()
    
    return plt

# Example usage:
results = [0.31, 0.000164, 0.01103125]  # Example values
platforms = ['MVVM-Vanilla', 'MVVM-GPU', 'SGLang-GPU']

# Create and save the plot
plot = plot_graph(results, platforms)
plot.savefig('sglang_vs_mvvm.pdf')
plot.close()