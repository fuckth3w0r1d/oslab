#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc == 4 && strcmp(argv[1], "-s") == 0) {
    if (symlink(argv[2], argv[3]) < 0)
      fprintf(2, "symlink %s %s: failed\n", argv[2], argv[3]);
    exit(0);
  }
  if (argc != 3) {
    fprintf(2, "Usage: ln [-s] old new\n");
    exit(1);
  }
  if (link(argv[1], argv[2]) < 0)
    fprintf(2, "link %s %s: failed\n", argv[1], argv[2]);
  exit(0);
}
