#include "kernel/types.h"
#include "user/user.h"

// If user/user.h declares pause(int) instead, change sleep to pause here.
#define WAIT_TICKS pause

int
main(int argc, char *argv[])
{
  if (argc != 2 || argv[1][0] == '\0') {
    fprintf(2, "usage: sleep ticks\n");
    exit(1);
  }

  // Validate the whole argument, including signed-int overflow.
  int checked = 0;
  for (int i = 0; argv[1][i] != '\0'; i++) {
    char c = argv[1][i];
    if (c < '0' || c > '9') {
      fprintf(2, "sleep: ticks must be a non-negative integer\n");
      exit(1);
    }
    int digit = c - '0';
    if (checked > (2147483647 - digit) / 10) {
      fprintf(2, "sleep: ticks is too large\n");
      exit(1);
    }
    checked = checked * 10 + digit;
  }

  int ticks = atoi(argv[1]);
  if (WAIT_TICKS(ticks) < 0) {
    fprintf(2, "sleep: interrupted or failed\n");
    exit(1);
  }
  exit(0);
}
