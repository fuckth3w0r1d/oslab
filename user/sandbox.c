#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc < 3) {
    fprintf(2, "usage: sandbox mask command [args...]\n");
    exit(1);
  }

  if (argv[1][0] == '\0') {
    fprintf(2, "sandbox: empty mask\n");
    exit(1);
  }

  // 此启动程序接受 int 正数范围内的十进制掩码。
  int mask = 0;
  for (int i = 0; argv[1][i] != '\0'; i++) {
    char c = argv[1][i];

    if (c < '0' || c > '9') {
      fprintf(2, "sandbox: mask must be a non-negative integer\n");
      exit(1);
    }

    int digit = c - '0';
    if (mask > (2147483647 - digit) / 10) {
      fprintf(2, "sandbox: mask is too large\n");
      exit(1);
    }

    mask = mask * 10 + digit;
  }

  if (interpose(mask, "-") < 0) {
    fprintf(2, "sandbox: interpose failed\n");
    exit(1);
  }

  exec(argv[2], &argv[2]);

  fprintf(2, "sandbox: exec %s failed\n", argv[2]);
  exit(1);
}
