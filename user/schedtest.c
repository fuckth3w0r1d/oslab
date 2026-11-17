// schedtest.c - user-space scheduling tests for xv6-riscv (2026).
// Tests are observational unless the kernel exports scheduling controls.
#include "kernel/types.h"
#include "user/user.h"

#define MAX_WORKERS 12
#define HIGH_PRIORITY 0
#define LOW_PRIORITY 3

// Set to 1 after implementing a user-visible setpriority(pid, priority).
// Adjust sched_setpriority() to match your group's syscall signature.
#define HAVE_SETPRIORITY 0

#if HAVE_SETPRIORITY
extern int setpriority(int pid, int priority);
#endif

struct result {
  int pid;
  int priority;
  int work;
  int elapsed;
};

static volatile unsigned int checksum;

static int
sched_setpriority(int priority)
{
#if HAVE_SETPRIORITY
  return setpriority(getpid(), priority);
#else
  (void)priority;
  return -1;
#endif
}

static void
workload(int ticks, struct result *r)
{
  int start = uptime();
  int loops = 0;
  unsigned int x = (unsigned int)getpid() + 1;
  // Check the clock periodically to make the test CPU-bound.
  while(uptime() - start < ticks){
    for(int j = 0; j < 1024; j++)
      x = x * 1664525U + 1013904223U;
    loops++;
  }
  checksum = x;
  r->work = loops;
  r->elapsed = uptime() - start;
}

static int
write_all(int fd, const void *buf, int n)
{
  const char *p = (const char *)buf;
  while(n > 0){
    int k = write(fd, p, n);
    if(k <= 0) return -1;
    p += k;
    n -= k;
  }
  return 0;
}

static int
read_all(int fd, void *buf, int n)
{
  char *p = (char *)buf;
  while(n > 0){
    int k = read(fd, p, n);
    if(k <= 0) return -1;
    p += k;
    n -= k;
  }
  return 0;
}

// All children wait on a start barrier. This keeps the measurement windows
// roughly aligned and makes scheduling differences easier to observe.
static int
run_group(const char *name, int n, int ticks, int use_priority,
          struct result out[])
{
  int startpipe[2], readypipe[2], resultpipe[2];
  int created = 0;
  char token = 'x';
  int failed = 0;

  printf("\n%s: %d workers, %d ticks\n", name, n, ticks);
  if(n < 1 || n > MAX_WORKERS) return -1;
  if(pipe(startpipe) < 0) return -1;
  if(pipe(readypipe) < 0){
    close(startpipe[0]); close(startpipe[1]);
    return -1;
  }
  if(pipe(resultpipe) < 0){
    close(startpipe[0]); close(startpipe[1]);
    close(readypipe[0]); close(readypipe[1]);
    return -1;
  }

  for(int i = 0; i < n; i++){
    int pid = fork();
    if(pid < 0){
      printf("  FAIL: fork\n");
      failed = 1;
      break;
    }
    if(pid == 0){
      struct result r;
      char c;
      close(startpipe[1]);
      close(readypipe[0]);
      close(resultpipe[0]);
      r.pid = getpid();
      r.priority = (i < n / 2) ? HIGH_PRIORITY : LOW_PRIORITY;
      r.work = 0;
      r.elapsed = 0;
      if(use_priority && sched_setpriority(r.priority) < 0){
        // Negative work signals that the scheduler API is unavailable.
        r.work = -1;
        write_all(resultpipe[1], &r, sizeof(r));
        exit(1);
      }
      if(write_all(readypipe[1], &token, 1) < 0)
        exit(1);
      close(readypipe[1]);
      if(read_all(startpipe[0], &c, 1) < 0)
        exit(1);
      close(startpipe[0]);
      workload(ticks, &r);
      if(write_all(resultpipe[1], &r, sizeof(r)) < 0)
        exit(1);
      close(resultpipe[1]);
      exit(0);
    }
    created++;
  }

  close(startpipe[0]);
  close(readypipe[1]);
  close(resultpipe[1]);

  if(!failed){
    for(int i = 0; i < created; i++){
      if(read_all(readypipe[0], &token, 1) < 0){
        failed = 1;
        break;
      }
    }
  }
  close(readypipe[0]);

  for(int i = 0; i < created; i++)
    if(write_all(startpipe[1], &token, 1) < 0)
      break;
  close(startpipe[1]);

  for(int i = 0; i < created; i++){
    if(read_all(resultpipe[0], &out[i], sizeof(out[i])) < 0){
      failed = 1;
      break;
    }
  }
  close(resultpipe[0]);
  for(int i = 0; i < created; i++){
    int status = 0;
    if(wait(&status) < 0 || status != 0)
      failed = 1;
  }
  if(created != n) failed = 1;
  if(failed){
    printf("  FAIL: group did not complete\n");
    return -1;
  }

  for(int i = 0; i < n; i++)
    printf("  pid=%d priority=%d work=%d elapsed=%d\n",
           out[i].pid, out[i].priority, out[i].work, out[i].elapsed);
  return 0;
}

static void
basic_test(void)
{
  struct result r[MAX_WORKERS];
  printf("schedtest: basic scheduling\n");
  if(run_group("basic", 6, 30, 0, r) < 0) return;
  for(int i = 0; i < 6; i++){
    if(r[i].work <= 0){
      printf("basic: FAIL (a process made no progress)\n");
      return;
    }
  }
  printf("basic: OK\n");
}

static void
priority_test(void)
{
  struct result r[MAX_WORKERS];
  int high = 0, low = 0;
  if(!HAVE_SETPRIORITY){
    printf("\npriority: SKIP (implement sched_setpriority first)\n");
    return;
  }
  if(run_group("priority", 8, 60, 1, r) < 0) return;
  for(int i = 0; i < 8; i++){
    if(r[i].priority == HIGH_PRIORITY) high += r[i].work;
    else low += r[i].work;
  }
  printf("priority: high_work=%d low_work=%d\n", high, low);
  // An observed ratio is not a deterministic preemption assertion.
  if(high > low) printf("priority: OBSERVED high > low\n");
  else printf("priority: CHECK high-priority tasks did not do more work\n");
}

static void
aging_test(void)
{
  struct result r[MAX_WORKERS];
  int low_progress = 1;
  if(!HAVE_SETPRIORITY){
    printf("\naging: SKIP (implement sched_setpriority first)\n");
    return;
  }
  if(run_group("aging", 10, 120, 1, r) < 0) return;
  for(int i = 0; i < 10; i++)
    if(r[i].priority == LOW_PRIORITY && r[i].work <= 0)
      low_progress = 0;
  if(low_progress)
    printf("aging: PROGRESS (low-priority tasks ran; aging not proven)\n");
  else
    printf("aging: CHECK (some low-priority tasks made no progress)\n");
}

static void
smp_test(void)
{
  struct result one[MAX_WORKERS], many[MAX_WORKERS];
  int total = 0;
  if(run_group("single-worker baseline", 1, 40, 0, one) < 0) return;
  if(run_group("multi-worker", 10, 40, 0, many) < 0) return;
  for(int i = 0; i < 10; i++) total += many[i].work;
  printf("smp: one_worker=%d ten_workers_total=%d\n", one[0].work, total);
  printf("smp: INFO (throughput only; CPU migration/stealing not proven)\n");
}

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf("usage: schedtest basic|priority|aging|smp|all\n");
    exit(1);
  }
  if(strcmp(argv[1], "basic") == 0) basic_test();
  else if(strcmp(argv[1], "priority") == 0) priority_test();
  else if(strcmp(argv[1], "aging") == 0) aging_test();
  else if(strcmp(argv[1], "smp") == 0) smp_test();
  else if(strcmp(argv[1], "all") == 0){
    basic_test();
    priority_test();
    aging_test();
    smp_test();
  } else {
    printf("unknown test: %s\n", argv[1]);
    exit(1);
  }
  exit(0);
}
