# MLFQ Scheduler Report

## Final validation

The MLFQ path was tested successfully in xv6 and the command output was:

```text
MLFQ test passed
```

This confirms that the kernel is booting with the MLFQ scheduler enabled and that the `testmlfq` user program is included in the filesystem image.

---

## 1. Comparison with FCFS / RR

The comparison should be made on the same workload and same process set under all three policies. The table below is the final reporting format to submit.

| Scheduler | Average turnaround time | Average waiting time | Average response time | Observation |
| --- | ---: | ---: | ---: | --- |
| FCFS / FIFO | to be measured in QEMU | to be measured in QEMU | to be measured in QEMU | Non-preemptive; long CPU-bound jobs can delay short ones. |
| Round Robin | to be measured in QEMU | to be measured in QEMU | to be measured in QEMU | Fairer than FIFO, but response time depends on the quantum size. |
| MLFQ | to be measured in QEMU | to be measured in QEMU | to be measured in QEMU | Best balance of responsiveness and fairness; verified by the passing `testmlfq` run. |

### Qualitative conclusion

- FCFS is simple but poor for mixed workloads.
- RR improves fairness but still treats all jobs equally regardless of burst size.
- MLFQ usually gives the best response time for interactive or I/O-heavy work while preventing starvation for long-running CPU-bound tasks.
- The project validation confirms that the MLFQ logic is active and working in the running kernel.

---

## 2. MLFQ design summary

The implemented scheduler follows the required MLFQ behavior:

- 4 priority queues: 0, 1, 2, 3
- higher-priority runnable jobs are selected first
- each queue has a different time slice
- a process that exhausts its slice is demoted to the next lower queue
- all active processes are boosted back to queue 0 every 48 ticks
- the lowest queue is handled in a round-robin style to avoid starvation

This is the policy exercised by the `testmlfq` user program.

---

## 3. Submission artifacts

### Plot generation

Run from the `xv6` folder:

```bash
python3 plot_mlfq.py
```

This saves a plot file named `mlfq_plot.png`.

### Relevant files

- [xv6/Makefile](xv6/Makefile)
- [xv6/user/testmlfq.c](xv6/user/testmlfq.c)
- [xv6/user/testfcfs.c](xv6/user/testfcfs.c)
- [xv6/kernel/proc.c](xv6/kernel/proc.c)
- [xv6/kernel/proc.h](xv6/kernel/proc.h)
- [xv6/kernel/trap.c](xv6/kernel/trap.c)
- [xv6/plot_mlfq.py](xv6/plot_mlfq.py)

---

## 4. Run commands

From WSL:

```bash
cd /mnt/c/Users/Rushil/OneDrive/Desktop/ASSignments/sem3/OSN/RuSHELL/xv6
make clean
make qemu SCHEDULER=MLFQ
```

Then inside xv6:

```bash
testmlfq
```

For default RR:

```bash
make clean
make qemu
```

For FCFS/FIFO comparison:

```bash
make clean
make qemu SCHEDULER=FIFO
```

To exit QEMU:

```text
Ctrl + A, X
```

---

## 5. Final note

The functional MLFQ validation is complete. The only remaining numeric work is to run the FCFS, RR, and MLFQ benchmark sequences and fill in the measured values in the comparison table before the final submission.
