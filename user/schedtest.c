#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

// Burn cpu for n ticks and return when it started.
static int
spin_ticks(int n)
{
  int start = uptime();
  while (uptime() - start < n)
    ;
  return start;
}

// A low priority process must be preempted as soon as a high priority
// one becomes runnable, so the high priority one finishes right away
// instead of waiting for the long running one to end.
static int
t_preempt(void)
{
  if (fork() == 0) {
    setpriority(3);
    spin_ticks(40);
    exit(0);
  }

  if (fork() == 0) {
    pause(2);
    setpriority(0);
    int start = spin_ticks(1);
    int used = uptime() - start;
    if (used > 8) {
      printf("schedtest preempt: FAIL high priority needed %d ticks\n", used);
      exit(1);
    }
    exit(0);
  }

  int status[2];
  for (int i = 0; i < 2; i++)
    wait(&status[i]);
  return status[0] == 0 && status[1] == 0 ? 0 : 1;
}

// A process that blocks every time slice keeps its level, one that never
// blocks sinks towards the lowest level.
static int
t_feedback(void)
{
  if (fork() == 0) {
    int p0 = getpriority();
    for (int i = 0; i < 4; i++)
      pause(1);
    int p1 = getpriority();
    if (p1 != p0) {
      printf("schedtest feedback: FAIL blocking process went from %d to %d\n",
             p0, p1);
      exit(1);
    }
    exit(0);
  }

  if (fork() == 0) {
    spin_ticks(20);
    int p1 = getpriority();
    if (p1 <= 0) {
      printf("schedtest feedback: FAIL cpu bound process stayed at level %d\n",
             p1);
      exit(1);
    }
    exit(0);
  }

  int status[2];
  for (int i = 0; i < 2; i++)
    wait(&status[i]);
  return status[0] == 0 && status[1] == 0 ? 0 : 1;
}

// One spinner per cpu holds level 0 for good; the low priority processes
// can only finish because the aging mechanism lifts them periodically.
static int
t_aging(void)
{
  int nlow = 4;
  int hogs[NCPU];

  for (int i = 0; i < 4; i++) {
    int pid = fork();
    if (pid == 0) {
      for (;;) {
        setpriority(0);
        spin_ticks(1);
      }
    }
    hogs[i] = pid;
  }

  for (int i = 0; i < nlow; i++) {
    if (fork() == 0) {
      setpriority(3);
      spin_ticks(2);
      exit(0);
    }
  }

  int start = uptime();
  for (int i = 0; i < nlow; i++) {
    int status;
    wait(&status);
    if (status != 0) {
      printf("schedtest aging: FAIL low priority child exited with %d\n",
             status);
      return 1;
    }
  }
  int used = uptime() - start;

  for (int i = 0; i < 4; i++)
    kill(hogs[i]);

  if (used > 200) {
    printf("schedtest aging: FAIL low priority took %d ticks\n", used);
    return 1;
  }
  return 0;
}

int
main(int argc, char **argv)
{
  int status;

  if (argc != 2) {
    printf("schedtest: expected one of preempt, feedback, aging\n");
    exit(1);
  }

  if (strcmp(argv[1], "preempt") == 0)
    status = t_preempt();
  else if (strcmp(argv[1], "feedback") == 0)
    status = t_feedback();
  else if (strcmp(argv[1], "aging") == 0)
    status = t_aging();
  else {
    printf("schedtest: unknown test %s\n", argv[1]);
    exit(1);
  }

  if (status == 0)
    printf("schedtest %s: OK\n", argv[1]);
  exit(status);
}
