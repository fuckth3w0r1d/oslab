#ifndef XV6_RUNQUEUE_H
#define XV6_RUNQUEUE_H
/* Requires types.h, param.h, spinlock.h included first. */
#define NQUEUE 4
#define MLFQ_BOOST_TICKS 100
struct proc;
struct mlfq_level {
  struct proc *q[NPROC];
  int n;
};

#include "rqstats.h"
struct runqueue {
  struct spinlock lock;
  struct mlfq_level levels[NQUEUE];
  int total;
  struct rq_stats stats;
};
extern struct runqueue runqueues[NCPU];
void rq_init(void);
void rq_enqueue(int cpu, struct proc *p); /* p->lock held */
struct proc *rq_pop(int cpu);             /* no locks held */
int rq_steal(int cpu);                    /* no locks held */
void rq_boost_all(void);                  /* CPU0 clock, every 100 ticks */
void rq_account_dispatch(int cpu);
void rq_snapshot(struct rq_stats dst[NCPU]);
int rq_total(int cpu);
#endif
