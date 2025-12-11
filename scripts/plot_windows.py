#!/usr/bin/env python3
"""
Plot script for windows.pdf
Generates a grouped bar chart comparing MVVM vs Native execution time
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

# Read data
df = pd.read_csv('data.csv')

# Extract benchmark names (simplify the command names)
def extract_name(cmd):
    if 'linpack' in cmd:
        return 'linpack'
    elif 'llama' in cmd:
        return 'llama'
    elif 'rgbd_tum' in cmd:
        return 'rgbd_tum'
    elif 'bt-' in cmd:
        return 'bt'
    elif 'cg-' in cmd:
        return 'cg'
    elif 'ep-' in cmd:
        return 'ep'
    elif 'ft-' in cmd:
        return 'ft'
    elif 'lu-' in cmd:
        return 'lu'
    elif 'mg-' in cmd:
        return 'mg'
    elif 'sp-' in cmd:
        return 'sp'
    elif 'redis' in cmd:
        return 'redis'
    return cmd

df['benchmark'] = df['name'].apply(extract_name)

# Group by benchmark and calculate mean and std
grouped = df.groupby('benchmark').agg({
    'mvvm': ['mean', 'std'],
    'native': ['mean', 'std']
}).reset_index()

# Flatten column names
grouped.columns = ['benchmark', 'mvvm_mean', 'mvvm_std', 'native_mean', 'native_std']

# Define the order of benchmarks (same as in the original figure, excluding 'ep')
order = ['linpack', 'llama', 'rgbd_tum', 'bt', 'cg', 'ft', 'lu', 'mg', 'sp', 'redis']
grouped['order'] = grouped['benchmark'].apply(lambda x: order.index(x) if x in order else len(order))
grouped = grouped.sort_values('order')
# Remove 'ep' from the data
grouped = grouped[grouped['benchmark'] != 'ep']

# Plot settings - use larger fonts for OSDI double-column template
# Figure will be scaled to ~3.3 inches width, so we need larger fonts
FONTSIZE = 24
plt.rcParams['font.size'] = FONTSIZE
plt.rcParams['axes.labelsize'] = FONTSIZE
plt.rcParams['axes.titlesize'] = FONTSIZE
plt.rcParams['xtick.labelsize'] = FONTSIZE
plt.rcParams['ytick.labelsize'] = FONTSIZE
plt.rcParams['legend.fontsize'] = FONTSIZE - 2
plt.rcParams['font.family'] = 'sans-serif'
plt.rcParams['font.weight'] = 'normal'
plt.rcParams['axes.linewidth'] = 1.5

# Smaller figure size = less scaling in paper = larger apparent font
fig, ax = plt.subplots(figsize=(10, 5))

x = np.arange(len(grouped))
width = 0.35

# Colors: blue and orange
color_mvvm = '#1f77b4'  # blue
color_native = '#ff7f0e'  # orange

# Create bars with error bars
bars1 = ax.bar(x - width/2, grouped['mvvm_mean'], width,
               yerr=grouped['mvvm_std'], capsize=3,
               label='MVVM', color=color_mvvm)
bars2 = ax.bar(x + width/2, grouped['native_mean'], width,
               yerr=grouped['native_std'], capsize=3,
               label='Native', color=color_native)

# Labels and formatting
ax.set_ylabel('Time(s)')
ax.set_xticks(x)
ax.set_xticklabels(grouped['benchmark'], rotation=30, ha='right')
ax.legend(loc='upper right', ncol=2, framealpha=0.9)

# Set y-axis limit
ax.set_ylim(0, 220)

# Add grid for better readability
ax.yaxis.grid(True, linestyle='--', alpha=0.3)
ax.set_axisbelow(True)

# Add value labels for benchmarks with very short bars (lu, sp)
def add_short_bar_labels(benchmark, y_mvvm, y_native):
    idx = list(grouped['benchmark']).index(benchmark)
    mvvm_val = grouped[grouped['benchmark'] == benchmark]['mvvm_mean'].values[0]
    native_val = grouped[grouped['benchmark'] == benchmark]['native_mean'].values[0]
    ax.annotate(f'{mvvm_val:.1e}', xy=(idx - width/2, y_mvvm), ha='center', va='bottom', fontsize=14, color=color_mvvm)
    ax.annotate(f'{native_val:.1e}', xy=(idx + width/2, y_native), ha='center', va='bottom', fontsize=14, color=color_native)

add_short_bar_labels('lu', 5, 18)
add_short_bar_labels('sp', 12, 25)

plt.tight_layout()
plt.savefig('windows.pdf', format='pdf', bbox_inches='tight')
plt.savefig('windows.png', format='png', dpi=150, bbox_inches='tight')
print("Saved windows.pdf and windows.png")
