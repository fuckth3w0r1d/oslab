#ifndef XV6_RQSTATS_H
#define XV6_RQSTATS_H
/* Include kernel/types.h before this file in user programs. */
struct rq_stats {
  uint64 attempts, successes, stolen_in, stolen_out;
  uint64 dispatches, lock_acquires, lock_contended;
  int queued;
};
#endif
