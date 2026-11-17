#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  char buf[512];
  int n;

  while ((n = read(0, buf, sizeof(buf))) > 0) {
    int written = 0;

    while (written < n) {
      int m = write(1, buf + written, n - written);

      if (m <= 0) {
        fprintf(2, "copy: write error\n");
        exit(1);
      }

      written += m;
    }
  }

  if (n < 0) {
    fprintf(2, "copy: read error\n");
    exit(1);
  }

  exit(0);
}
