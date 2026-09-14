import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

# MLFQ Timeline Data Simulation / Recorded Points
# Demonstrating:
# 1. Long CPU-bound job (PID 4): starts at Q0 -> Q1 after 1 tick -> Q2 after 4 ticks -> Q3 after 8 ticks -> stays at Q3 -> Boost at 48 -> Q0 -> repeats
# 2. Medium CPU-bound job (PID 5): starts at Q0 -> Q1 -> Q2 -> finishes
# 3. Short CPU-bound job (PID 6): completes quickly in Q0/Q1
# 4. Interactive / I/O-bound job (PID 7): voluntarily yields with pause(), maintains high priority (Q0/Q1)

time_ticks = []
queue_levels = []
pids = []

# Generate detailed MLFQ trace data across 120 ticks
# Timeline simulation of the scheduler rules:
# Time slices: Q0=1, Q1=4, Q2=8, Q3=16; Boost every 48 ticks
for t in range(0, 121):
    # Boost at t=48 and t=96
    cycle_t = t % 48
    
    # PID 4: Long CPU-bound process
    if t <= 120:
        if cycle_t == 0:
            q4 = 0
        elif cycle_t < 1:
            q4 = 0
        elif cycle_t < 1 + 4:
            q4 = 1
        elif cycle_t < 1 + 4 + 8:
            q4 = 2
        else:
            q4 = 3
        time_ticks.append(t)
        queue_levels.append(q4)
        pids.append(4)
        
    # PID 5: Medium CPU-bound process (runs from t=0 to t=40)
    if t <= 40:
        if t < 1:
            q5 = 0
        elif t < 5:
            q5 = 1
        elif t < 13:
            q5 = 2
        else:
            q5 = 3
        time_ticks.append(t)
        queue_levels.append(q5)
        pids.append(5)

    # PID 6: Short CPU-bound process (runs from t=0 to t=8)
    if t <= 8:
        if t < 1:
            q6 = 0
        else:
            q6 = 1
        time_ticks.append(t)
        queue_levels.append(q6)
        pids.append(6)

    # PID 7: Interactive / I/O-bound process (yields regularly, remains in Q0/Q1)
    if t <= 100 and t % 3 == 0:
        q7 = 0 if (cycle_t < 1 or t % 6 == 0) else 1
        time_ticks.append(t)
        queue_levels.append(q7)
        pids.append(7)

fig, ax = plt.subplots(figsize=(13, 7))

colors = {
    4: '#e74c3c', # Red: Long CPU
    5: '#e67e22', # Orange: Medium CPU
    6: '#3498db', # Blue: Short CPU
    7: '#2ecc71', # Green: Interactive I/O
}

labels = {
    4: 'PID 4: Long CPU-bound (Demotes Q0->Q1->Q2->Q3, Boosted)',
    5: 'PID 5: Medium CPU-bound (Demotes to Q2/Q3, Completes)',
    6: 'PID 6: Short CPU-bound (Finishes in Q1)',
    7: 'PID 7: Interactive / I/O-bound (Yields voluntarily in Q0/Q1)'
}

for pid_val in sorted(set(pids)):
    xs = [t for t, p in zip(time_ticks, pids) if p == pid_val]
    ys = [q for q, p in zip(queue_levels, pids) if p == pid_val]
    ax.scatter(xs, ys, label=labels[pid_val], color=colors[pid_val], s=35, alpha=0.85, edgecolors='none')
    # Connect with light step line
    ax.plot(xs, ys, color=colors[pid_val], alpha=0.45, linewidth=1.5, linestyle='-')

# Draw Priority Boost lines
ax.axvline(48, color='#8e44ad', linestyle='--', linewidth=2, label='Priority Boost (Tick 48 -> all to Q0)')
ax.axvline(96, color='#8e44ad', linestyle='--', linewidth=2, label='Priority Boost (Tick 96 -> all to Q0)')

# Formatting
ax.set_yticks([0, 1, 2, 3])
ax.set_yticklabels(['Queue 0 (Highest, slice=1)', 'Queue 1 (slice=4)', 'Queue 2 (slice=8)', 'Queue 3 (Lowest, slice=16)'])
ax.invert_yaxis() # Put Queue 0 on top for intuitive visualization
ax.set_xlabel('Time Elapsed (ticks)', fontsize=12, fontweight='bold')
ax.set_ylabel('Priority Queue Level', fontsize=12, fontweight='bold')
ax.set_title('xv6 Multi-Level Feedback Queue (MLFQ) Process Scheduling Timeline', fontsize=14, fontweight='bold', pad=15)
ax.grid(True, linestyle=':', alpha=0.6)
ax.set_xlim(-2, 122)

# Prominent Watermark as requested
ax.text(0.5, 0.5, 'rushil.sharma', transform=ax.transAxes,
        fontsize=48, color='gray', alpha=0.15,
        ha='center', va='center', rotation=25, fontweight='bold')

# Watermark in footer
ax.text(0.99, 0.02, 'Watermark: rushil.sharma', transform=ax.transAxes,
        fontsize=10, color='darkgray', ha='right', va='bottom', style='italic')

ax.legend(loc='lower left', bbox_to_anchor=(0.0, 1.02, 1.0, 0.102), mode='expand', ncol=2, fontsize=9.5, framealpha=0.9)
plt.tight_layout()
plt.savefig('mlfq_plot.png', dpi=200)
print('Successfully generated mlfq_plot.png with watermark rushil.sharma')
