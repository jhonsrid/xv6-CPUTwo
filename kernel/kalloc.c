// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "cputwo.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char _end[]; // first address after kernel BSS; defined by TCC linker as _end.

struct run {
  struct run *next;
};

struct spinlock kmem_lock;
struct run *kmem_freelist;

void
kinit()
{
  printf("kinit: _end=0x%x PHYSTOP=0x%x\n", (uint32)_end, (uint32)PHYSTOP);
  initlock(&kmem_lock, "kmem");
  printf("kinit: freelist before freerange=0x%x\n", (uint32)kmem_freelist);
  freerange(_end, (void*)PHYSTOP);
  printf("kinit: freelist after freerange=0x%x\n", (uint32)kmem_freelist);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint32)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
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

  if(((uint32)pa % PGSIZE) != 0 || (char*)pa < _end || (uint32)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem_lock);
  r->next = kmem_freelist;
  kmem_freelist = r;
  release(&kmem_lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem_lock);
  r = kmem_freelist;
  if(r)
    kmem_freelist = r->next;
  release(&kmem_lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  printf("kalloc: returning 0x%x\n", (uint32)r);
  return (void*)r;
}
