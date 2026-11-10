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

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];   // give each core a lock and freelist

void
kinit()
{ // // original-code
  // initlock(&kmem.lock,"kmem");
  char lock_name[10];
  for (int i=0; i<NCPU; i++){
    snprintf(lock_name, 10, "lock_%d",i);  // give each cpu lock a name "lock_i"
    initlock(&kmem[i].lock, lock_name);   // init the cpu lock
  }
  freerange(end, (void *)PHYSTOP);
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
  // // original-code-------------------------------
  // acquire(&kmem.lock);
  // r->next = kmem.freelist;
  // kmem.freelist = r;
  // release(&kmem.lock);
  // // --------------------------------------------
  push_off();   // disable interrupt, for getting cpu id
  int cpu = cpuid();
  pop_off();    // enable interrupt 
  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  // // original-code--------------------
  // acquire(&kmem.lock);
  // r = kmem.freelist;
  // if (r)
  //   kmem.freelist = r->next;
  // release(&kmem.lock);
  // // ---------------------------------
  push_off();
  int cpu = cpuid();
  pop_off();
  acquire(&kmem[cpu].lock);
  r = kmem[cpu].freelist;
  if(r){    // own freelist is not empty
    kmem[cpu].freelist = r->next; 
    release(&kmem[cpu].lock);
  }
  else{    // own empty, try to steal other cpu's freelist
    release(&kmem[cpu].lock);   // release own lock, avoid having 2 locks
    for(int i = 0;i < NCPU;i++){ 
      if(i == cpu){   // skip now cpu itself
        continue; 
      }
      acquire(&kmem[i].lock); 
      if(kmem[i].freelist == 0){  // this new cpu's freelist is empty too, skip 
        release(&kmem[i].lock); 
        continue; 
      }
      
      struct run *tmp = kmem[i].freelist; 
      for(int j = 0; j < 512; j++){   // get the last "not empty" list node
        if(tmp->next)
          tmp = tmp->next;
        else
          break;
      }
      struct run* head = kmem[i].freelist;  // record the head of the "stolen" list
      kmem[i].freelist = tmp->next;   // upgrade the kmem[i] freelist
      tmp->next = 0;                  // set the "tail" signal
      release(&kmem[i].lock);         // switch cpu lock, avoid having 2 locks
      acquire(&kmem[cpu].lock);       
      r = head; 
      kmem[cpu].freelist = r->next;   
      release(&kmem[cpu].lock);
      break;
    }
  }

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}
