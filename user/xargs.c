#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

int main(int argc, char* argv[]) {
  char* cmd_args[MAXARG];
  char buf[256] = {0};
  int i = 0;
  int len = 0;

  if (argc < 2) {
    fprintf(2, "No arguments\n");
    exit(1);
  } else if (argc + 1 > MAXARG) {
    fprintf(2, "too many args\n");
    exit(1);
  }
  for (i = 1; i < argc; i++) cmd_args[i - 1] = argv[i];
  cmd_args[argc] = 0;

  for (;;) {
    i = 0;
    for (;;) {
      len = read(0, buf + i, 1);
      if (len == 0 || buf[i] == '\n') break;
      ++i;
    }
    if (i == 0) break;
    buf[i] = '\0';
    cmd_args[argc - 1] = buf;
    if (fork() == 0) {
      exec(cmd_args[0], cmd_args);
      exit(0);
    } else {
      wait(0);
    }
  }

  exit(0);
}
