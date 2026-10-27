#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc != 1) {
    fprintf(2, "usage: ksupertest\n");
    exit(1);
  }

  if (ksupercheck() < 0) {
    fprintf(2, "ksupertest: FAILED\n");
    exit(1);
  }

  printf("ksupertest: ALL CHECKS PASSED\n");
  exit(0);
}
