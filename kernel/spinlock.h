// Mutual exclusion lock.
struct spinlock {
  uint locked; // Is the lock held?

  // For debugging:
  char *name;      // Name of lock.
  struct cpu *cpu; // The cpu holding the lock.
#ifdef LAB_LOCK
  int nts;
  int n;
#endif
};

#ifdef LAB_LOCK
#define RWLOCK_READ   1ULL
#define RWLOCK_WAIT   (1ULL << 32)
#define RWLOCK_HELD   (1ULL << 63)
#define RWLOCK_READS  0xffffffffULL
#define RWLOCK_WAITS  0x7fffffff00000000ULL

// Reader-writer lock.
struct rwspinlock {
  uint64 state;
};
#endif
