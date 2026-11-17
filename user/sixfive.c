#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define TOKEN_SIZE 512

static void
print_if_match(char *token, int length)
{
  if (length == 0)
    return;

  int mod5 = 0;
  int mod6 = 0;
  for (int i = 0; i < length; i++) {
    if (token[i] < '0' || token[i] > '9')
      return;
    int digit = token[i] - '0';
    mod5 = (mod5 * 10 + digit) % 5;
    mod6 = (mod6 * 10 + digit) % 6;
  }

  if (mod5 == 0 || mod6 == 0) {
    token[length] = '\0';
    printf("%s\n", token);
  }
}

int
main(int argc, char *argv[])
{
  char *path = argv[1];
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    fprintf(2, "sixfive: cannot open %s\n", path);
    exit(1);
  }

  char input[512];
  char token[TOKEN_SIZE];
  int length = 0;
  int too_long = 0;
  int failed = 0;
  int n;

  while ((n = read(fd, input, sizeof(input))) > 0) {
    for (int i = 0; i < n; i++) {
      char c = input[i];
      if (c != '\0' && strchr(" -\r\t\n./,", c) != 0) {
        if (too_long) {
          fprintf(2, "sixfive: token exceeds 511 bytes; skipped\n");
          failed = 1;
        } else {
          print_if_match(token, length);
        }
        length = 0;
        too_long = 0;
      } else if (!too_long) {
        if (length < TOKEN_SIZE - 1)
          token[length++] = c;
        else
          too_long = 1;
      }
    }
  }

  if (n < 0) {
    fprintf(2, "sixfive: read failed\n");
    failed = 1;
  } else if (too_long) {
    fprintf(2, "sixfive: token exceeds 511 bytes; skipped\n");
    failed = 1;
  } else {
    print_if_match(token, length);
  }

  close(fd);
  exit(failed);
}
