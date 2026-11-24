#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/rqstats.h"
#include "user/user.h"
#define CHILDREN 20
#define ROUNDS 4

static void
burn(int n)
{
  int start = uptime();
  volatile uint x = 0;
  while (uptime() - start < n)
    x++;
}

static int
reap(int n)
{
  int bad = 0;
  for (int i = 0; i < n; i++) {
    int st = -1;
    if (wait(&st) < 0 || st != 0)
      bad = 1;
  }
  return bad;
}

static int
t_feedback(void)
{
  int n = 0;
  int pid = fork();
  if (pid < 0) return 1;
  if (pid == 0) {
    int before = getpriority();
    for (int i = 0; i < 4; i++)
      pause(1);
    int after = getpriority();
    if (after != before) {
      printf("feedback: blocking priority %d -> %d\n", before, after);
      exit(1);
    }
    exit(0);
  }
  n++;
  pid = fork();
  if (pid < 0) return 1;
  if (pid == 0) {
    burn(16);
    int level = getpriority();
    if (level == 0) {
      printf("feedback: CPU-bound task never demoted\n");
      exit(1);
    }
    exit(0);
  }
  n++;
  return reap(n);
}

/* On one CPU the high-priority child should get CPU within several ticks. */
static int
t_preempt(void)
{
  int n = 0;
  int pid = fork();
  if (pid < 0) return 1;
  if (pid == 0) {
    if (setpriority(3) < 0) exit(1);
    burn(25);
    exit(0);
  }
  n++;
  pid = fork();
  if (pid < 0) return 1;
  if (pid == 0) {
    pause(2);
    if (setpriority(0) < 0) exit(1);
    int start = uptime();
    burn(1);
    int elapsed = uptime() - start;
    if (elapsed > 8) {
      printf("preempt: high priority waited %d ticks\n", elapsed);
      exit(1);
    }
    exit(0);
  }
  n++;
  return reap(n);
}

/* The hogs explicitly keep asking for level 0. Low tasks need boosts. */
static int
t_aging(void)
{
  int hogs[4];
  int nh = 0, nl = 0, bad = 0;
  for (int i = 0; i < 4; i++) {
    int pid = fork();
    if (pid < 0) { bad = 1; break; }
    if (pid == 0) {
      for (;;) {
        setpriority(0);
        burn(1);
      }
    }
    hogs[nh++] = pid;
  }
  if (bad) goto cleanup;
  for (int i = 0; i < 4; i++) {
    int pid = fork();
    if (pid < 0) { bad = 1; break; }
    if (pid == 0) {
      if (setpriority(3) < 0) exit(1);
      burn(2);
      exit(0);
    }
    nl++;
  }
  int start = uptime();
  if (reap(nl)) bad = 1;
  if (uptime() - start > 500) {
    printf("aging: low priority exceeded 500 ticks\n");
    bad = 1;
  }
cleanup:
  for (int i = 0; i < nh; i++)
    kill(hogs[i]);
  if (reap(nh)) {
    /* Kill exits with -1; do not treat this as failure. */
  }
  return bad;
}

/* Similar to kalloctest: test completion plus measurable kernel counters. */
static int
t_steal(void)
{
  struct rq_stats before[NCPU], after[NCPU];
  if (getrqstats(before) < 0) return 1;
  int spawned = 0, bad = 0;
  for (int i = 0; i < CHILDREN; i++) {
    int pid = fork();
    if (pid < 0) { bad = 1; break; }
    if (pid == 0) {
      burn(12);
      exit(0);
    }
    spawned++;
  }
  if (reap(spawned)) bad = 1;
  if (spawned != CHILDREN) bad = 1;
  if (getrqstats(after) < 0) return 1;
  uint64 attempts=0, success=0, ins=0, outs=0;
  printf("CPU attempt success in out dispatch lock contended\n");
  for (int c = 0; c < NCPU; c++) {
    uint64 a=after[c].attempts-before[c].attempts;
    uint64 s=after[c].successes-before[c].successes;
    uint64 in=after[c].stolen_in-before[c].stolen_in;
    uint64 out=after[c].stolen_out-before[c].stolen_out;
    uint64 r=after[c].dispatches-before[c].dispatches;
    uint64 l=after[c].lock_acquires-before[c].lock_acquires;
    uint64 k=after[c].lock_contended-before[c].lock_contended;
    printf("%d %ld %ld %ld %ld %ld %ld %ld\n",c,(long)a,(long)s,
           (long)in,(long)out,(long)r,(long)l,(long)k);
    attempts+=a; success+=s; ins+=in; outs+=out;
  }
  printf("steal: children=%d/%d attempts=%ld successes=%ld\n",
         spawned,CHILDREN,(long)attempts,(long)success);
  /* Snapshot per CPU is not a global atomic snapshot; migration counts
     can have slight skew if unrelated concurrent activity occurs. */
  if (success > attempts) bad = 1;
  if (success == 0) {
    printf("steal: INCONCLUSIVE (no successful migrations observed)\n");
    bad = 1;
  }
  if (ins != outs || ins != success)
    printf("steal: note non-atomic snapshot (%ld in, %ld out)\n",(long)ins,(long)outs);
  return bad;
}

static int
t_wakeup(void)
{
  int n=0, bad=0;
  for (int i=0;i<8;i++) {
    int pid=fork();
    if (pid<0) { bad=1; break; }
    if (pid==0) {
      for(int j=0;j<5;j++) { pause(1); burn(1); }
      exit(0);
    }
    n++;
  }
  if(reap(n)) bad=1;
  return bad || n!=8;
}

static int
t_stress(void)
{
  for (int round=0;round<ROUNDS;round++) {
    int n=0, bad=0;
    for(int i=0;i<CHILDREN;i++) {
      int pid=fork();
      if(pid<0) { bad=1; break; }
      if(pid==0) {
        if(i%2==0) burn(3);
        else for(int j=0;j<3;j++){ pause(1); burn(1); }
        exit(0);
      }
      n++;
    }
    if(reap(n)) bad=1;
    if(bad || n!=CHILDREN) return 1;
  }
  return 0;
}

static int
run(char *name, int (*fn)(void))
{
  printf("schedtest %s: start\n",name);
  int result=fn();
  printf("schedtest %s: %s\n",name,result?"FAIL":"OK");
  return result;
}

int
main(int argc, char **argv)
{
  if(argc!=2){
    printf("usage: schedtest preempt|feedback|aging|steal|wakeup|stress|all\n");
    exit(1);
  }
  char *name=argv[1];
  int bad=0;
  if(strcmp(name,"preempt")==0) bad=run(name,t_preempt);
  else if(strcmp(name,"feedback")==0) bad=run(name,t_feedback);
  else if(strcmp(name,"aging")==0) bad=run(name,t_aging);
  else if(strcmp(name,"steal")==0) bad=run(name,t_steal);
  else if(strcmp(name,"wakeup")==0) bad=run(name,t_wakeup);
  else if(strcmp(name,"stress")==0) bad=run(name,t_stress);
  else if(strcmp(name,"all")==0) {
    bad|=run("preempt",t_preempt);
    bad|=run("feedback",t_feedback);
    bad|=run("aging",t_aging);
    bad|=run("steal",t_steal);
    bad|=run("wakeup",t_wakeup);
    bad|=run("stress",t_stress);
  } else {printf("schedtest: unknown case\n");exit(1);}
  exit(bad);
}
