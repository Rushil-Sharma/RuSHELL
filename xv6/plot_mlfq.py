import matplotlib.pyplot as plt
import numpy as np

# Example data
time = [0, 10, 20, 30]
queue = [0, 1, 2, 0]
pid = [1, 1, 1, 2]

plt.figure(figsize=(10, 6))
for p in sorted(set(pid)):
    xs = [t for t, qid, current_pid in zip(time, queue, pid) if current_pid == p]
    ys = [q for q, current_pid in zip(queue, pid) if current_pid == p]
    plt.plot(xs, ys, marker='o', label=f'PID {p}')

plt.axhline(0, color='black', linewidth=0.5)
plt.axhline(1, color='black', linewidth=0.5)
plt.axhline(2, color='black', linewidth=0.5)
plt.axhline(3, color='black', linewidth=0.5)

plt.xlabel("Time (ticks)")
plt.ylabel("Queue ID")
plt.title("MLFQ Trace")
plt.grid(True)
plt.legend()
plt.text(0.02, 0.02, "rushil.sharma", transform=plt.gca().transAxes,
         fontsize=10, alpha=0.4)
plt.tight_layout()
plt.savefig("mlfq_plot.png")
plt.show()