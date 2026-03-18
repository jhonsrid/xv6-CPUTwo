#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "cputwo.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"

/*
 * The kernel's page table.
 */
pagetable_t kernel_pagetable;

// trampoline_start is the page-aligned start of uservec/userret code (trampoline.S).
// Its link address equals its physical address under identity mapping.
extern char trampoline_start[];

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // Map the entire MMIO region R/W (bypasses MMU anyway, but needed in PT).
  kvmmap(kpgtbl, 0x03F00000u, 0x03F00000u, 0x100000u, PTE_R | PTE_W);

  // Map all physical RAM R/W/X (kernel code + data, identity-mapped).
  kvmmap(kpgtbl, KERNBASE, KERNBASE, PHYSTOP - KERNBASE, PTE_R | PTE_W | PTE_X);

  // Map the region between PHYSTOP and MMIO_BASE as R/W.
  // The CRT boot stack lives just below MMIO_BASE (0x03F00000) regardless
  // of PHYSTOP, so we must map this range to avoid page faults after paging
  // is enabled.  (MMIO itself bypasses the MMU in hardware.)
  if(PHYSTOP < 0x03F00000u) {
    // Only map the actual boot stack page (entry.S sets sp=0x03EFFFFC).
    // A large identity-map here would conflict with kstack/trampoline mappings.
    kvmmap(kpgtbl, 0x03EFF000u, 0x03EFF000u, PGSIZE, PTE_R | PTE_W);
  }

  // Map the trampoline at the highest virtual address (same in kernel and user).
  // The physical page hosting the trampoline code is trampoline_start (page-aligned).
  kvmmap(kpgtbl, TRAMPOLINE, (uint32)trampoline_start, PGSIZE, PTE_R | PTE_X);

  // Allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}

// Add a mapping to the kernel page table.
// Only used during boot; does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint32 va, uint32 pa, uint32 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Initialise the kernel page table shared by all CPUs.
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch this CPU's page table register to the kernel page table and enable paging.
void
kvminithart(void)
{
  // Writing SATP implicitly flushes non-global TLB entries.
  w_satp(MAKE_SATP(kernel_pagetable));
}

// Return the address of the PTE in page table 'pagetable' that corresponds
// to virtual address 'va'.  If alloc != 0, create any required page-table pages.
//
// CPUTwo Sv32: two levels, 1024 PTEs each, 32-bit entries, 4 KB pages.
//   VA[31:22] = L1 index (10 bits)
//   VA[21:12] = L2 index (10 bits)
//   VA[11:0]  = page offset (12 bits)
pte_t *
walk(pagetable_t pagetable, uint32 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  // Only one intermediate level: start at L1, descend to L2 leaf.
  for(int level = 1; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address; return the physical address, or 0 if not mapped.
// Can only be used to look up user pages.
uint32
walkaddr(pagetable_t pagetable, uint32 va)
{
  pte_t *pte;
  uint32 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// Create PTEs for virtual addresses [va, va+size) mapping to [pa, pa+size).
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint32 va, uint32 size, uint32 pa, int perm)
{
  uint32 a, last;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("mappages: va not aligned");
  if((size % PGSIZE) != 0)
    panic("mappages: size not aligned");
  if(size == 0)
    panic("mappages: size");

  a = va;
  last = va + size - PGSIZE;
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Create an empty user page table.
// Returns 0 if out of memory.
pagetable_t
uvmcreate(void)
{
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Remove npages of mappings starting from va. va must be page-aligned.
// It's OK if the mappings don't exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint32 va, uint32 npages, int do_free)
{
  uint32 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      continue;
    if((*pte & PTE_V) == 0)
      continue;
    if(do_free){
      uint32 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// Allocate PTEs and physical memory to grow process from oldsz to newsz.
// Returns new size or 0 on error.
uint32
uvmalloc(pagetable_t pagetable, uint32 oldsz, uint32 newsz, int xperm)
{
  char *mem;
  uint32 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint32)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages from oldsz down to newsz.
// Returns new size.
uint32
uvmdealloc(pagetable_t pagetable, uint32 oldsz, uint32 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // Sv32: 1024 PTEs per level.
  for(int i = 0; i < 1024; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // Non-leaf: points to a lower-level page table.
      uint32 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages, then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint32 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Copy parent's page table and physical memory into child's page table.
// Returns 0 on success, -1 on failure (frees allocated pages on failure).
int
uvmcopy(pagetable_t old, pagetable_t new, uint32 sz)
{
  pte_t *pte;
  uint32 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      continue;
    if((*pte & PTE_V) == 0)
      continue;
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint32)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// Mark a PTE invalid for user access (used for user stack guard page).
void
uvmclear(pagetable_t pagetable, uint32 va)
{
  pte_t *pte;
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy len bytes from src to virtual address dstva in pagetable.
// Returns 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint32 dstva, char *src, uint32 len)
{
  uint32 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;

    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0){
      if((pa0 = vmfault(pagetable, va0, 0)) == 0)
        return -1;
    }

    pte = walk(pagetable, va0, 0);
    if((*pte & PTE_W) == 0)
      return -1;

    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void*)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy len bytes to dst from virtual address srcva in pagetable.
// Returns 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint32 srcva, uint32 len)
{
  uint32 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0){
      if((pa0 = vmfault(pagetable, va0, 0)) == 0)
        return -1;
    }
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void*)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a NUL-terminated string from user to kernel.
// Returns 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint32 srcva, uint32 max)
{
  uint32 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char*)(pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n; --max; p++; dst++;
    }
    srcva = va0 + PGSIZE;
  }
  return got_null ? 0 : -1;
}

// Allocate and map a lazily-allocated user page on fault.
// Returns physical address on success, 0 on failure.
uint32
vmfault(pagetable_t pagetable, uint32 va, int read)
{
  uint32 mem;
  struct proc *p = myproc();

  if(va >= p->sz)
    return 0;
  va = PGROUNDDOWN(va);
  if(ismapped(pagetable, va))
    return 0;
  mem = (uint32)kalloc();
  if(mem == 0)
    return 0;
  memset((void*)mem, 0, PGSIZE);
  if(mappages(p->pagetable, va, PGSIZE, mem, PTE_W|PTE_U|PTE_R) != 0){
    kfree((void*)mem);
    return 0;
  }
  return mem;
}

int
ismapped(pagetable_t pagetable, uint32 va)
{
  pte_t *pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  return (*pte & PTE_V) ? 1 : 0;
}
