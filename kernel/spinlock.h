// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
#ifdef LAB_LOCK
  int nts;
  int n;
#endif
};

#ifdef LAB_LOCK
// Reader-writer lock.
struct rwspinlock {
  uint readers;          // Number of registered readers.
  uint writer;           // 1 if a writer has reserved exclusive access.
  uint waiting_writers;  // Number of writers waiting for that reservation.
};
#endif
