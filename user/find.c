#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *name(char *path) {
  char *p;
  // Find first character after last slash.
  for (p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;
  return p;
}

int find(char *path, char *filename) {
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;
  char* n;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return -1;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return -1;
  }

  switch (st.type) {
    case T_FILE:
      if (!strcmp(filename, name(path))) printf("%s\n", path);
      break;

    case T_DIR:
      if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        printf("find: path too long\n");
        break;
      }
      strcpy(buf, path);
      p = buf + strlen(buf);
      *p++ = '/';
      while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        if (stat(buf, &st) < 0) {
          printf("find: cannot stat %s\n", buf);
          continue;
        }
        n = name(buf);
        if (!strcmp(n, ".") || !strcmp(n, "..")) continue;
        find(buf, filename);
      }
      break;
  }
  close(fd);
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(2, "Usage: find [path] [filename]\n");
    exit(1);
  }
  if (find(argv[1], argv[2]) < 0) {
    fprintf(2, "Unable to find\n");
    exit(1);
  }
  exit(0);
}