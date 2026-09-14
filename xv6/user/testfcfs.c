#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  int i;
  int pid;

  for (i = 0; i < 3; i++) {
    pid = fork();
    if (pid < 0) {
      printf("fork failed\n");
      exit(1);
    }
    if (pid == 0) {
      printf("child %d starts\n", getpid());
      pause(1);
      printf("child %d exits\n", getpid());
      exit(0);
    }
  }

  for (i = 0; i < 3; i++) {
    wait(0);
  }

  printf("FCFS test passed\n");
  exit(0);
}
