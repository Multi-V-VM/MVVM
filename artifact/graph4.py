import matplotlib.pyplot as plt
import numpy as np


def plot_graph(results1, results2, labels):
    font = {'size': 40}
    plt.rc('font', **font)
    # Define colors for each platform
    # Create figure and axis
    fig, ax = plt.subplots(figsize=(10, 10))
    
    # Create bar positions
    x = np.arange(len(results1))
    
    # Create bars with specified colors
    bars1 = ax.bar(x, results1, width=0.1)
    bars2 = ax.bar(x+0.1, results2, width=0.1)
    
    # Color each bar according to the platform
    for bar, label in zip(bars1, labels):
        bar.set_color( 'cyan') 
        bar.set_label('MVVM') # Default to gray if color not found
    for bar, label in zip(bars2, labels):
        bar.set_color( 'purple')  # Default to gray if color not found
        bar.set_label('CrewAI')
    # Customize the plot
    ax.set_ylabel('Latency (s)')
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=45, fontsize=20)
    
    # Add value labels on top of each bar
    for bar in bars1:
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2., height,
                f'{height:.2f}',
                ha='center', rotation=45, va='bottom',fontsize=20)
    
    for bar in bars2:
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2., height,
                f'{height:.2f}',
                ha='center', rotation=45, va='bottom',fontsize=20)
    
    # Add grid for better readability
    ax.grid(True, axis='y', linestyle='--', alpha=0.7)
    
    # Adjust layout to prevent label cutoff
    plt.tight_layout()
    
    return plt

# Example usage:
results1 = [7.414, 142.142, 60.214]  # Example values
results2 = [8.823, 178.635, 78.644]  # Example values
platforms = ["job-post","long_file_translate","write_seo_blog_humanize"]

# Create and save the plot
plot = plot_graph(results1, results2, platforms)
plot.savefig('crewai_vs_openai.pdf')
plot.close()