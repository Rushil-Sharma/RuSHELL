#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  int i;
  for (i = 0; i < 4; i++) {
    int pid = fork();
    if (pid == 0) {
      int j;
      for (j = 0; j < 10; j++) {
        volatile int x = 0;
        int k;
        for (k = 0; k < 50000000; k++)
          x++;
        printf("child %d burst %d\n", getpid(), j);
      }
      exit(0);
    }
  }

  for (i = 0; i < 4; i++)
    wait(0);

  printf("MLFQ test passed\n");
  exit(0);
}
