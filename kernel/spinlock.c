// Mutual exclusion spin locks.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

#ifdef LAB_LOCK
#define NLOCK 500

static struct spinlock *locks[NLOCK];
struct spinlock lock_locks;

void
freelock(struct spinlock *lk)
{
  acquire(&lock_locks);
  int i;
  for (i = 0; i < NLOCK; i++) {
    if (locks[i] == lk) {
      locks[i] = 0;
      break;
    }
  }
  release(&lock_locks);
}

static void
findslot(struct spinlock *lk)
{
  acquire(&lock_locks);
  int i;
  for (i = 0; i < NLOCK; i++) {
    if (locks[i] == 0) {
      locks[i] = lk;
      release(&lock_locks);
      return;
    }
  }
  panic("findslot");
}
#endif

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
#ifdef LAB_LOCK
  lk->nts = 0;
  lk->n = 0;
  findslot(lk);
#endif
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void
acquire(struct spinlock *lk)
{
  push_off(); // disable interrupts to avoid deadlock.
  if (holding(lk))
    panic("acquire");

#ifdef LAB_LOCK
  __atomic_fetch_add(&lk->n, 1, __ATOMIC_RELAXED);
#endif

  // On RISC-V, __atomic_exchange_n turns into an atomic swap:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  //
  // Passing __ATOMIC_ACQUIRE to __atomic_exchange_n tells
  // the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen strictly after the lock is acquired.
  while (__atomic_exchange_n(&lk->locked, 1, __ATOMIC_ACQUIRE) != 0) {
#ifdef LAB_LOCK
    __atomic_fetch_add(&lk->nts, 1, __ATOMIC_RELAXED);
#else
    ;
#endif
  }

  // Record info about lock acquisition for holding() and debugging.
  lk->cpu = mycpu();
}

// Release the lock.
void
release(struct spinlock *lk)
{
  if (!holding(lk))
    panic("release");

  lk->cpu = 0;

  // Release the lock, equivalent to lk->locked = 0.
  //
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  //
  // On RISC-V, __atomic_store_n turns into a single atomic store:
  //   s1 = &lk->locked
  //   fence rw,w
  //   sw zero,0(s1)
  //
  // The __ATOMIC_RELEASE argument to __atomic_store_n tells the
  // the C compiler and the CPU to not move loads or stores past
  // this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released,
  // and that loads in the critical section occur strictly before
  // the lock is released.
  //
  // On RISC-V, this generates a fence instruction before the store:
  //   fence rw,w
  __atomic_store_n(&lk->locked, 0, __ATOMIC_RELEASE);

  pop_off();
}

#ifdef LAB_LOCK
static void
read_acquire_inner(struct rwspinlock *rwlk)
{
  uint64 s;

  while (1) {
    s = __atomic_load_n(&rwlk->state, __ATOMIC_RELAXED);
    if (s & (RWLOCK_HELD | RWLOCK_WAITS))
      continue;
    if (__atomic_compare_exchange_n(&rwlk->state, &s, s + RWLOCK_READ, 1,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
      return;
  }
}

static void
read_release_inner(struct rwspinlock *rwlk)
{
  __atomic_fetch_sub(&rwlk->state, RWLOCK_READ, __ATOMIC_RELEASE);
}

static void
write_acquire_inner(struct rwspinlock *rwlk)
{
  uint64 s;

  __atomic_fetch_add(&rwlk->state, RWLOCK_WAIT, __ATOMIC_RELAXED);
  while (1) {
    s = __atomic_load_n(&rwlk->state, __ATOMIC_RELAXED);
    if (s & (RWLOCK_HELD | RWLOCK_READS))
      continue;
    if (__atomic_compare_exchange_n(&rwlk->state, &s,
                                    (s - RWLOCK_WAIT) | RWLOCK_HELD, 1,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
      return;
  }
}

static void
write_release_inner(struct rwspinlock *rwlk)
{
  __atomic_fetch_and(&rwlk->state, ~RWLOCK_HELD, __ATOMIC_RELEASE);
}

void
initrwlock(struct rwspinlock *rwlk)
{
  rwlk->state = 0;
}

void
read_acquire(struct rwspinlock *rwlk)
{
  push_off(); // disable interrupts to avoid deadlock.
  read_acquire_inner(rwlk);
}

void
read_release(struct rwspinlock *rwlk)
{
  read_release_inner(rwlk);
  pop_off();
}

void
write_acquire(struct rwspinlock *rwlk)
{
  push_off(); // disable interrupts to avoid deadlock.
  write_acquire_inner(rwlk);
}

void
write_release(struct rwspinlock *rwlk)
{
  write_release_inner(rwlk);
  pop_off();
}

// Test rwspinlock implementation.
static uint barrier;

static void
rwspinlock_test_step(uint step, const char *msg)
{
  const uint ncpu = 4;

  __atomic_fetch_add(&barrier, 1, __ATOMIC_ACQ_REL);
  while (__atomic_load_n(&barrier, __ATOMIC_RELAXED) < ncpu * step) {
    // spin
  }

  if (cpuid() == 0) {
    printk("rwspinlock_test: step %d: %s\n", step, msg);
  }
}

static uint
delay()
{
  static uint v;
  for (int i = 0; i < 10000; i++) {
    __atomic_fetch_add(&v, 1, __ATOMIC_RELAXED);
  }
  return __atomic_load_n(&v, __ATOMIC_RELAXED);
}

static void
long_delay()
{
  for (int i = 0; i < 10; i++) {
    delay();
  }
}

uint64
sys_rwlktest()
{
  int resetflag;
  int r = 0;
  int step = 0;

  argint(0, &resetflag);
  if (resetflag == 1) {
    __atomic_store_n(&barrier, 0, __ATOMIC_SEQ_CST);
    return 0;
  }

  push_off();
  int id = cpuid();

  rwspinlock_test_step(++step, "initrwlock");

  static struct rwspinlock l;
  if (id == 0) {
    initrwlock(&l);
  }

  rwspinlock_test_step(++step, "concurrent read_acquire");

  for (int i = 0; i < 1000000; i++)
    read_acquire(&l);

  rwspinlock_test_step(++step, "concurrent read_release");

  for (int i = 0; i < 1000000; i++)
    read_release(&l);

  rwspinlock_test_step(++step, "wait-free read acquire/release");

  // This test ensures that the read_acquire and read_release
  // implementations are wait-free.  Roughly speaking, this
  // means that the implementation should not take any locks,
  // and instead should use atomic instructions directly.
  // More formally, this means that if one CPU is running
  // read_acquire or read_release, and pauses for some reason
  // at any point in its execution, other CPUs should still
  // be able to successfully run read_acquire and read_release.
  //
  // Here, the pausing is implemented by forcing CPUs to take
  // monitor-mode traps, which bypass supervisor-level intr_off().
  // Specifically, hart IDs 0 and 1 will preempt IDs 2 and 3.
  if (id == 0 || id == 1) {
    int buddy = id + 2;
    int preempted = 0;

    for (int i = 0; i < 1000000; i++) {
      preempted = !preempted;

      *(uint32*) (CLINT(buddy)) = preempted;
      read_acquire(&l);
      read_release(&l);
    }

    preempted = 0;
    *(uint32*) (CLINT(buddy)) = preempted;
  } else {
    for (int i = 0; i < 1000000; i++) {
      read_acquire(&l);
      read_release(&l);
    }
  }

  rwspinlock_test_step(++step, "prepare read_acquire for writer priority test");

  if (id == 1) {
    for (int i = 0; i < 30; i++) {
      read_acquire(&l);
    }
  }

  rwspinlock_test_step(++step, "writer priority test");

  static uint flag;
  if (id == 0) {
    write_acquire(&l);
    __atomic_store_n(&flag, 1, __ATOMIC_RELAXED);
    write_release(&l);
  }

  if (id == 1) {
    delay();
    for (int i = 0; i < 10; i++) {
      read_release(&l);
    }
    delay();
    for (int i = 0; i < 10; i++) {
      read_release(&l);
    }
    delay();
    for (int i = 0; i < 10; i++) {
      read_release(&l);
    }
  }

  if (id == 2) {
    delay();
    read_acquire(&l);
    uint f = __atomic_load_n(&flag, __ATOMIC_RELAXED);
    if (f == 0) {
      printk("rwspinlock_test: reader sneaked ahead of waiting writer\n");
      r = -1;
    }
    read_release(&l);
  }

  rwspinlock_test_step(++step, "checking for concurrent readers/writers");

  static uint v;
  if (id == 0) {
    uint maxwv = 0;
    for (int i = 0; i < 1000000; i++) {
      write_acquire(&l);
      uint x = __atomic_add_fetch(&v, 1, __ATOMIC_ACQ_REL);
      if (x > maxwv) {
        maxwv = x;
      }
      uint y = __atomic_fetch_sub(&v, 1, __ATOMIC_ACQ_REL);
      if (y > maxwv) {
        maxwv = y;
      }
      write_release(&l);
    }
    if (maxwv > 1) {
      printk("rwspinlock_test: cpu %d saw concurrent reads/writes: %d\n", id,
             maxwv);
      r = -1;
    }
  } else {
    uint maxrv = 0;
    for (int i = 0; i < 1000000; i++) {
      read_acquire(&l);
      uint x = __atomic_add_fetch(&v, 1, __ATOMIC_ACQ_REL);
      if (x > maxrv) {
        maxrv = x;
      }
      uint y = __atomic_fetch_sub(&v, 1, __ATOMIC_ACQ_REL);
      if (y > maxrv) {
        maxrv = y;
      }
      read_release(&l);
    }
    if (maxrv < 2) {
      printk("rwspinlock_test: cpu %d never saw concurrent reads: %d\n", id,
             maxrv);
      r = -1;
    }
  }

  rwspinlock_test_step(++step, "checking for concurrent writers");

  uint maxwv = 0;
  for (int i = 0; i < 1000000; i++) {
    write_acquire(&l);
    uint x = __atomic_add_fetch(&v, 1, __ATOMIC_ACQ_REL);
    if (x > maxwv) {
      maxwv = x;
    }
    uint y = __atomic_fetch_sub(&v, 1, __ATOMIC_ACQ_REL);
    if (y > maxwv) {
      maxwv = y;
    }
    write_release(&l);
  }
  if (maxwv > 1) {
    printk("rwspinlock_test: cpu %d saw concurrent writes: %d\n", id, maxwv);
    r = -1;
  }

  rwspinlock_test_step(++step, "acquiring multiple locks");

  struct rwspinlock l2;
  initrwlock(&l2);
  write_acquire(&l2);
  read_acquire(&l);

  rwspinlock_test_step(++step, "releasing multiple locks");

  write_release(&l2);
  read_release(&l);

  for (int i = 0; i < 10; i++) {
    rwspinlock_test_step(
      ++step, "prepare read_acquire for multiple writer priority test");

    static uint writer_count;
    if (id == 3) {
      writer_count = 0;
      read_acquire(&l);
      read_acquire(&l);
    }

    rwspinlock_test_step(++step, "multiple writer priority test");

    if (id == 0 || id == 1) {
      write_acquire(&l);
      writer_count++;
      delay();
      write_release(&l);
    }

    if (id == 2) {
      long_delay();
      read_acquire(&l);
      if (writer_count == 0) {
        printk(
          "rwspinlock_test: reader sneaked ahead of both waiting writers\n");
        r = -1;
      }
      delay();
      delay();
      delay();
      read_release(&l);
    }

    if (id == 3) {
      delay();
      read_release(&l);
      delay();
      read_release(&l);

      long_delay();
      long_delay();

      // By this point, either one writer executed and CPU 2 is holding read lock,
      // or both writers executed.  Should never sneak ahead of second writer.
      read_acquire(&l);
      if (writer_count != 2) {
        printk(
          "rwspinlock_test: reader sneaked ahead of second waiting writer\n");
        r = -1;
      }
      read_release(&l);
    }
  }

  rwspinlock_test_step(++step, "done");

  printk("rwspinlock_test(%d): %d\n", id, r);
  pop_off();

  return r;
}
#endif

// Check whether this cpu is holding the lock.
// Interrupts must be off.
int
holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.

void
push_off(void)
{
  // disable interrupts to prevent an involuntary context
  // switch while using mycpu().
  uint64 flags = rc_sstatus(SSTATUS_SIE);
  int old = !!(flags & SSTATUS_SIE);

  if (mycpu()->noff == 0)
    mycpu()->intena = old;
  mycpu()->noff += 1;
}

void
pop_off(void)
{
  struct cpu *c = mycpu();
  if (intr_get())
    panic("pop_off - interruptible");
  if (c->noff < 1)
    panic("pop_off");
  c->noff -= 1;
  if (c->noff == 0 && c->intena)
    intr_on();
}

// Read a shared 32-bit value without holding a lock
int
atomic_read4(int *addr)
{
  uint32 val;
  __atomic_load(addr, &val, __ATOMIC_SEQ_CST);
  return val;
}

#ifdef LAB_LOCK
int
snprint_lock(char *buf, int sz, struct spinlock *lk)
{
  int n = 0;
  if (lk->n > 0) {
    n = snprintf(buf, sz, "lock: %s: #test-and-set %d #acquire() %d\n",
                 lk->name, lk->nts, lk->n);
  }
  return n;
}

int
statslock(char *buf, int sz)
{
  int n;
  int tot = 0;

  acquire(&lock_locks);
  n = snprintf(buf, sz, "--- lock kmem stats\n");
  for (int i = 0; i < NLOCK; i++) {
    if (locks[i] == 0)
      break;
    if (strncmp(locks[i]->name, "kmem", strlen("kmem")) == 0) {
      tot += locks[i]->nts;
      n += snprint_lock(buf + n, sz - n, locks[i]);
    }
  }

  n += snprintf(buf + n, sz - n, "--- top 5 contended locks:\n");
  int last = 100000000;
  // stupid way to compute top 5 contended locks
  for (int t = 0; t < 5; t++) {
    int top = 0;
    for (int i = 0; i < NLOCK; i++) {
      if (locks[i] == 0)
        break;
      if (locks[i]->nts > locks[top]->nts && locks[i]->nts < last) {
        top = i;
      }
    }
    n += snprint_lock(buf + n, sz - n, locks[top]);
    last = locks[top]->nts;
  }
  n += snprintf(buf + n, sz - n, "tot= %d\n", tot);
  release(&lock_locks);
  return n;
}
#endif
