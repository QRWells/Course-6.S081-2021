#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char const *argv[]) {
  int fds[11][2];
  int p;
  int n;

  for (int i = 0; i < 11; ++i) {
    if (i == 0) pipe(fds[i]);
    if (i < 10) pipe(fds[i + 1]);
    if (fork() == 0) {
      if (read(fds[i][0], &p, 1) == -1) {
        printf("Unable to read\n");
        exit(1);
      }
      printf("prime %d\n", p);
      while (read(fds[i][0], &n, 1)) {
        if (n == '#' && i < 10) {
          write(fds[i + 1][1], "#", 1);
          exit(0);
        }
        // p does not divide n
        if ((n % p) && i < 10) write(fds[i + 1][1], &n, 1);
      }
    }
    if (i < 10) close(fds[i + 1][1]);
    close(fds[i][0]);
  }

  for (int i = 2; i <= 35; i++) write(fds[0][1], &i, 1);

  write(fds[0][1], "#", 1);
  close(fds[0][1]);

  // wait for 11 children
  for (int i = 0; i < 11; i++) wait(0);

  exit(0);
}
