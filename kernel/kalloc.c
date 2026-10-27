// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);
void superfree(void *pa);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

#define NSUPERPG 32

struct run {
  struct run *next;
};

struct superrun {
  struct superrun *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  struct superrun *superlist;
} kmem;

void
kinit()
{
  uint64 sstart = PHYSTOP - (uint64)NSUPERPG * SUPERPGSIZE;

  initlock(&kmem.lock, "kmem");
  freerange(end, (void *)sstart);
  for (uint64 pa = sstart; pa < PHYSTOP; pa += SUPERPGSIZE)
    superfree((void *)pa);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}

void *
superalloc(void)
{
  struct superrun *r;

  acquire(&kmem.lock);
  r = kmem.superlist;
  if (r)
    kmem.superlist = r->next;
  release(&kmem.lock);

  if (r)
    memset((char *)r, 5, SUPERPGSIZE);
  return (void *)r;
}

void
superfree(void *pa)
{
  struct superrun *r;

  if (((uint64)pa % SUPERPGSIZE) != 0 ||
      (uint64)pa < PHYSTOP - (uint64)NSUPERPG * SUPERPGSIZE ||
      (uint64)pa >= PHYSTOP)
    panic("superfree");

  memset(pa, 1, SUPERPGSIZE);

  r = (struct superrun *)pa;

  acquire(&kmem.lock);
  r->next = kmem.superlist;
  kmem.superlist = r;
  release(&kmem.lock);
}
