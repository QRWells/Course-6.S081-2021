#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char const *argv[]) {
  int pid = 0;
  int n;
  int fd1[2];
  int fd2[2];
  char buf[8] = {0};

  pipe(fd1);
  pipe(fd2);

  if ((pid = fork()) > 0) {
    write(fd1[1], "T", 1);
    n = read(fd2[0], buf, 1);
    if (n == -1) {
      fprintf(2, "Parent read error!\n");
      exit(1);
    }
    close(fd1[0]);
    close(fd2[1]);
    printf("%d: received pong\n", getpid());
  } else if (pid == 0) {
    n = read(fd1[0], buf, 1);
    if (n == -1) {
      fprintf(2, "Child read error!\n");
      exit(1);
    }
    close(fd1[1]);
    close(fd2[0]);
    printf("%d: received ping\n", getpid());
    write(fd2[1], buf, n);
  } else {
    fprintf(2, "Unable to fork.\n");
    exit(1);
  }
  exit(0);
}
