import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# MLFQ trace data; replace with the exact recorded values if you collect a live trace from QEMU.
time = [0, 5, 10, 15, 20, 25, 30, 35, 40]
queue = [0, 0, 1, 1, 2, 2, 3, 3, 0]
pid = [1, 1, 1, 2, 2, 2, 3, 3, 3]

plt.figure(figsize=(10, 6))
for p in sorted(set(pid)):
    xs = [t for t, current_pid in zip(time, pid) if current_pid == p]
    ys = [q for q, current_pid in zip(queue, pid) if current_pid == p]
    plt.plot(xs, ys, marker='o', linewidth=2, label=f'PID {p}')

for q in range(4):
    plt.axhline(q, color='black', linewidth=0.5, alpha=0.5)

plt.xlim(min(time) - 1, max(time) + 1)
plt.ylim(-0.5, 3.5)
plt.xlabel('Time (ticks)')
plt.ylabel('Queue ID')
plt.title('MLFQ Trace')
plt.grid(True, linestyle='--', alpha=0.4)
plt.legend()
plt.text(0.02, 0.02, 'rushil.sharma', transform=plt.gca().transAxes, fontsize=10, alpha=0.4)
plt.tight_layout()
plt.savefig('mlfq_plot.png', dpi=150)
print('Saved mlfq_plot.png')
