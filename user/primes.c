#include "kernel/types.h"
#include "user/user.h"

// Return 1 for an integer, 0 for clean EOF, and -1 for an error.
static int
read_int(int fd, int *value)
{
  int done = 0;
  int size = sizeof(*value);
  while (done < size) {
    int n = read(fd, (char *)value + done, size - done);
    if (n < 0)
      return -1;
    if (n == 0)
      return done == 0 ? 0 : -1;
    done += n;
  }
  return 1;
}

static int
write_int(int fd, int value)
{
  int done = 0;
  int size = sizeof(value);
  while (done < size) {
    int n = write(fd, (char *)&value + done, size - done);
    if (n <= 0)
      return -1;
    done += n;
  }
  return 0;
}

static void sieve(int input) __attribute__((noreturn));

static void
sieve(int input)
{
  // Each child continues this loop as the next pipeline stage.
  for (;;) {
    int prime;
    int result = read_int(input, &prime);
    if (result != 1) {
      close(input);
      if (result < 0)
        fprintf(2, "primes: read failed\n");
      exit(result < 0);
    }
    printf("prime %d\n", prime);

    // Create another stage only if a surviving number exists.
    int number;
    while ((result = read_int(input, &number)) == 1) {
      if (number % prime != 0)
        break;
    }
    if (result != 1) {
      close(input);
      if (result < 0)
        fprintf(2, "primes: read failed\n");
      exit(result < 0);
    }

    int next[2];
    if (pipe(next) < 0) {
      fprintf(2, "primes: pipe failed\n");
      close(input);
      exit(1);
    }
    int pid = fork();
    if (pid < 0) {
      fprintf(2, "primes: fork failed\n");
      close(input);
      close(next[0]);
      close(next[1]);
      exit(1);
    }

    if (pid == 0) {
      close(input);
      close(next[1]);
      input = next[0];
      continue;
    }

    close(next[0]);
    int failed = write_int(next[1], number) < 0;
    while (!failed && (result = read_int(input, &number)) == 1) {
      if (number % prime != 0 && write_int(next[1], number) < 0)
        failed = 1;
    }
    if (result < 0)
      failed = 1;
    close(input);
    // Close BEFORE wait: the child needs EOF to finish.
    close(next[1]);

    int status = 0;
    if (wait(&status) < 0 || status != 0)
      failed = 1;
    if (failed)
      fprintf(2, "primes: pipeline failed\n");
    exit(failed);
  }
}

int
main(int argc, char *argv[])
{
  if (argc != 1) {
    fprintf(2, "usage: primes\n");
    exit(1);
  }

  int p[2];
  if (pipe(p) < 0) {
    fprintf(2, "primes: pipe failed\n");
    exit(1);
  }
  int pid = fork();
  if (pid < 0) {
    fprintf(2, "primes: fork failed\n");
    close(p[0]);
    close(p[1]);
    exit(1);
  }
  if (pid == 0) {
    close(p[1]);
    sieve(p[0]);
  }

  close(p[0]);
  int failed = 0;
  for (int number = 2; number <= 40; number++) {
    if (write_int(p[1], number) < 0) {
      failed = 1;
      break;
    }
  }
  close(p[1]);
  int status = 0;
  if (wait(&status) < 0 || status != 0)
    failed = 1;
  if (failed)
    fprintf(2, "primes: pipeline failed\n");
  exit(failed);
}
