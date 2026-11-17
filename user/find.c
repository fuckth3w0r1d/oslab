#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define PATH_SIZE 512

static int
match_action(char *path, char *cmd)
{
  if (cmd == 0) {
    printf("%s\n", path);
    return 0;
  }

  int pid = fork();
  if (pid < 0) {
    fprintf(2, "find: fork failed\n");
    return 1;
  }
  if (pid == 0) {
    // Do not give the command our open traversal directories.
    for (int fd = 3; fd < NOFILE; fd++)
      close(fd);
    char *args[3];
    args[0] = cmd;
    args[1] = path;
    args[2] = 0;
    exec(cmd, args);
    fprintf(2, "find: exec %s failed\n", cmd);
    exit(1);
  }

  int status = 0;
  if (wait(&status) < 0) {
    fprintf(2, "find: wait failed\n");
    return 1;
  }
  return status != 0;
}

static int
find_path(char *path, char *target, char *cmd)
{
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return 1;
  }
  struct stat st;
  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return 1;
  }

  if (st.type == T_FILE) {
    char *name = path;
    for (char *p = path; *p; p++) {
      if (*p == '/')
        name = p + 1;
    }
    close(fd);
    if (strcmp(name, target) == 0)
      return match_action(path, cmd);
    return 0;
  }
  if (st.type != T_DIR) {
    close(fd);
    return 0;
  }

  int failed = 0;
  int n;
  struct dirent de;
  while ((n = read(fd, &de, sizeof(de))) == sizeof(de)) {
    if (de.inum == 0)
      continue;

    char name[DIRSIZ + 1];
    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = '\0';
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      continue;

    char child[PATH_SIZE];
    int length = strlen(path);
    int separator = length > 0 && path[length - 1] != '/';
    if (length + separator + strlen(name) + 1 > sizeof(child)) {
      fprintf(2, "find: path too long under %s\n", path);
      failed = 1;
      continue;
    }

    memmove(child, path, length);
    if (separator)
      child[length++] = '/';
    strcpy(child + length, name);
    if (find_path(child, target, cmd) != 0)
      failed = 1;
  }
  if (n != 0) {
    fprintf(2, "find: directory read failed: %s\n", path);
    failed = 1;
  }
  close(fd);
  return failed;
}

int
main(int argc, char *argv[])
{
  if ((argc != 3 && argc != 5) ||
      (argc == 5 && strcmp(argv[3], "-exec") != 0)) {
    fprintf(2, "usage: find path name [-exec cmd]\n");
    exit(1);
  }
  if (argv[1][0] == '\0' || argv[2][0] == '\0' ||
      (argc == 5 && argv[4][0] == '\0')) {
    fprintf(2, "find: arguments must not be empty\n");
    exit(1);
  }

  char *cmd = argc == 5 ? argv[4] : 0;
  exit(find_path(argv[1], argv[2], cmd));
}
