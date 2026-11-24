#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "runqueue.h"

struct runqueue runqueues[NCPU];
extern struct proc proc[NPROC];

static void
qlock(struct runqueue *r)
{
  int busy = __atomic_load_n(&r->lock.locked, __ATOMIC_RELAXED);
  acquire(&r->lock);
  r->stats.lock_acquires++;
  if (busy)
    r->stats.lock_contended++; /* approximate contention */
}

static void
qunlock(struct runqueue *r)
{
  release(&r->lock);
}

void
rq_init(void)
{
  for (int c = 0; c < NCPU; c++) {
    struct runqueue *r = &runqueues[c];
    initlock(&r->lock, "runqueue");
    r->total = 0;
    r->stats = (struct rq_stats){0};
    for (int l = 0; l < NQUEUE; l++)
      r->levels[l].n = 0;
  }
}

/* All successful RUNNABLE transitions must call this with p->lock held. */
void
rq_enqueue(int cpu, struct proc *p)
{
  if (cpu < 0 || cpu >= NCPU || !holding(&p->lock) ||
      p->state != RUNNABLE || p->priority < 0 || p->priority >= NQUEUE)
    panic("rq_enqueue: state");
  struct runqueue *r = &runqueues[cpu];
  qlock(r);
  if (p->on_rq || r->total == NPROC)
    panic("rq_enqueue: duplicate/full");

  struct mlfq_level *q = &r->levels[p->priority];
  if (q->n >= NPROC)
    panic("rq_enqueue: level full");
  
  q->q[q->n++] = p;
  r->total++;
  p->on_rq = 1;
  p->rq_cpu = cpu;
  qunlock(r);
}

/* A removed RUNNABLE process is claimed by the caller after locking p.
   Other code must never enqueue RUNNABLE processes unless changing state. */
struct proc *
rq_pop(int cpu)
{
  struct runqueue *r = &runqueues[cpu];
  struct proc *p = 0;
  qlock(r);
  for (int l = 0; l < NQUEUE; l++) {
    struct mlfq_level *q = &r->levels[l];
    if (!q->n)
      continue;
    p = q->q[0];
    if (!p || !p->on_rq || p->rq_cpu != cpu)
      panic("rq_pop: corruption");
    for (int i = 1; i < q->n; i++)
      q->q[i-1] = q->q[i];
    q->q[--q->n] = 0;
    r->total--;
    p->on_rq = 0;
    p->rq_cpu = -1;
    break;
  }
  qunlock(r);
  return p;
}

static int
steal_from(int thief, int victim)
{
  struct runqueue *dst = &runqueues[thief];
  struct runqueue *src = &runqueues[victim];
  struct runqueue *first = thief < victim ? dst : src;
  struct runqueue *second = thief < victim ? src : dst;
  qlock(first);
  qlock(second);
  dst->stats.attempts++;
  if (dst->total || !src->total)
    goto failed;

  for (int l = NQUEUE - 1; l >= 0; l--) {
    struct mlfq_level *from = &src->levels[l];
    if (!from->n)
      continue;
    struct proc *p = from->q[from->n - 1];
    if (!p || !p->on_rq || p->rq_cpu != victim)
      panic("steal: corrupt entry");
    struct mlfq_level *to = &dst->levels[l];
    if (to->n >= NPROC)
      panic("steal: full");
    from->q[--from->n] = 0;
    src->total--;
    to->q[to->n++] = p;
    dst->total++;
    p->rq_cpu = thief;
    dst->stats.successes++;
    dst->stats.stolen_in++;
    src->stats.stolen_out++;
    qunlock(second);
    qunlock(first);
    return 1;
  }
failed:
  qunlock(second);
  qunlock(first);
  return 0;
}

int
rq_steal(int thief)
{
  if (thief < 0 || thief >= NCPU)
    return 0;
  for (int d = 1; d < NCPU; d++) {
    int victim = (thief + d) % NCPU;
    if (steal_from(thief, victim))
      return 1;
  }
  return 0;
}

/* Elevate all processes to level zero, including sleeping/running ones.
   p->lock -> ALL rq locks (ascending); stealing takes only rq locks.
   This is intentionally simple and costly; it runs infrequently. */
void
rq_boost_all(void)
{
  for (struct proc *p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == UNUSED || p->state == USED || p->state == ZOMBIE) {
      release(&p->lock);
      continue;
    }
    for (int c = 0; c < NCPU; c++)
      qlock(&runqueues[c]);

    if (p->on_rq) {
      int cpu = p->rq_cpu;
      if (cpu < 0 || cpu >= NCPU)
        panic("boost: cpu");
      struct runqueue *r = &runqueues[cpu];
      int found = 0;
      for (int l = 0; l < NQUEUE; l++) {
        struct mlfq_level *q = &r->levels[l];
        for (int i = 0; i < q->n; i++) {
          if (q->q[i] != p)
            continue;
          for (int j = i + 1; j < q->n; j++)
            q->q[j-1] = q->q[j];
          q->q[--q->n] = 0;
          found = 1;
          break;
        }
        if (found) break;
      }
      if (!found)
        panic("boost: missing queued process");
      struct mlfq_level *top = &r->levels[0];
      if (top->n >= NPROC)
        panic("boost: full");
      top->q[top->n++] = p;
      /* r->total and p->on_rq do not change. */
    }
    p->priority = 0;
    p->slice_used = 0;
    for (int c = NCPU - 1; c >= 0; c--)
      qunlock(&runqueues[c]);
    release(&p->lock);
  }
}

void
rq_account_dispatch(int cpu)
{
  struct runqueue *r = &runqueues[cpu];
  qlock(r);
  r->stats.dispatches++;
  qunlock(r);
}

void
rq_snapshot(struct rq_stats dst[NCPU])
{
  for (int c = 0; c < NCPU; c++) {
    struct runqueue *r = &runqueues[c];
    qlock(r);
    dst[c] = r->stats;
    dst[c].queued = r->total;
    qunlock(r);
  }
}

int
rq_total(int cpu)
{
  struct runqueue *r = &runqueues[cpu];
  qlock(r);
  int n = r->total;
  qunlock(r);
  return n;
}
