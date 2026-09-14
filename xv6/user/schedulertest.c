#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NUM_PROCS 5

void
cpu_work(int iterations)
{
  volatile int count = 0;
  for (int i = 0; i < iterations; i++) {
    for (int j = 0; j < 5000000; j++) {
      count++;
    }
  }
}

int
main(int argc, char *argv[])
{
  printf("Starting schedulertest with %d processes...\n", NUM_PROCS);

  for (int i = 0; i < NUM_PROCS; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("fork failed\n");
      exit(1);
    }
    if (pid == 0) {
      int mypid = getpid();
      if (i == 0) {
        // Process 0: Long CPU-bound
        printf("[PID %d] Starting Long CPU-bound workload\n", mypid);
        cpu_work(30);
      } else if (i == 1) {
        // Process 1: Medium CPU-bound
        printf("[PID %d] Starting Medium CPU-bound workload\n", mypid);
        cpu_work(15);
      } else if (i == 2) {
        // Process 2: Short CPU-bound
        printf("[PID %d] Starting Short CPU-bound workload\n", mypid);
        cpu_work(5);
      } else if (i == 3) {
        // Process 3: Interactive / I/O bound with pause
        printf("[PID %d] Starting Interactive workload 1\n", mypid);
        for (int step = 0; step < 10; step++) {
          cpu_work(1);
          pause(1);
        }
      } else {
        // Process 4: Interactive / I/O bound with pause
        printf("[PID %d] Starting Interactive workload 2\n", mypid);
        for (int step = 0; step < 10; step++) {
          cpu_work(1);
          pause(1);
        }
      }
      printf("[PID %d] Completed workload\n", mypid);
      exit(0);
    }
  }

  int total_turnaround = 0;
  int total_waiting = 0;
  int total_response = 0;

  printf("\n--- Collecting process completion statistics ---\n");
  for (int i = 0; i < NUM_PROCS; i++) {
    int status, wtime, rtime, resp;
    int exited_pid = waitx(&status, &wtime, &rtime, &resp);
    int ttime = wtime + rtime;

    printf("PID %d: Turnaround = %d ticks, Waiting = %d ticks, Response = %d ticks, Run = %d ticks\n",
           exited_pid, ttime, wtime, resp, rtime);

    total_turnaround += ttime;
    total_waiting += wtime;
    total_response += resp;
  }

  printf("\n================ SUMMARY METRICS ================\n");
  printf("Average Turnaround Time : %d ticks\n", total_turnaround / NUM_PROCS);
  printf("Average Waiting Time    : %d ticks\n", total_waiting / NUM_PROCS);
  printf("Average Response Time   : %d ticks\n", total_response / NUM_PROCS);
  printf("=================================================\n");

  exit(0);
}
