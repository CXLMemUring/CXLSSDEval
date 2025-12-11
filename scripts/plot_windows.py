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

# Define the order of benchmarks (same as in the original figure)
order = ['linpack', 'llama', 'rgbd_tum', 'bt', 'cg', 'ep', 'ft', 'lu', 'mg', 'sp', 'redis']
grouped['order'] = grouped['benchmark'].apply(lambda x: order.index(x) if x in order else len(order))
grouped = grouped.sort_values('order')

# Plot settings
plt.rcParams['font.size'] = 18
plt.rcParams['axes.labelsize'] = 18
plt.rcParams['axes.titlesize'] = 18
plt.rcParams['xtick.labelsize'] = 18
plt.rcParams['ytick.labelsize'] = 18
plt.rcParams['legend.fontsize'] = 18

fig, ax = plt.subplots(figsize=(14, 6))

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
ax.set_xticklabels(grouped['benchmark'])
ax.legend(loc='upper right')

# Set y-axis limit
ax.set_ylim(0, 200)

# Add grid for better readability
ax.yaxis.grid(True, linestyle='--', alpha=0.3)
ax.set_axisbelow(True)

plt.tight_layout()
plt.savefig('windows.pdf', format='pdf', bbox_inches='tight')
plt.savefig('windows.png', format='png', dpi=150, bbox_inches='tight')
print("Saved windows.pdf and windows.png")
