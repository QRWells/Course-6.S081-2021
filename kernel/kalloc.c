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
} kmem;

struct refs {
  struct spinlock lock;
  char cnt[PHYSTOP / PGSIZE]; // RC
} refs;

void kinit() {
  initlock(&kmem.lock, "kmem");
  initlock(&refs.lock, "ref");
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) {
    refs.cnt[(uint64)p / PGSIZE] = 1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&refs.lock);

  // if it is still referenced
  if (--refs.cnt[(uint64)pa / PGSIZE] != 0) {
    release(&refs.lock);
    return;
  }

  release(&refs.lock);

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
void *kalloc(void) {
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r) {
    kmem.freelist = r->next;

    acquire(&refs.lock);
    refs.cnt[(uint64)r / PGSIZE] = 1; // set the new page's ref count to 1
    release(&refs.lock);
  }
  release(&kmem.lock);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}

int kaddrefcnt(void *pa) {
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    return -1;
  acquire(&refs.lock);
  ++refs.cnt[(uint64)pa / PGSIZE];
  release(&refs.lock);
  return 0;
}

// return 0 if it is cow page, -1 otherwise
int cowpage(pagetable_t pagetable, uint64 va) {
  if (va >= MAXVA)
    return -1;
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_C) == 0)
    return -1;
  return 0;
}

// allocate page for va and return pa, or 0 if failed
void *cow_alloc(pagetable_t pagetable, uint64 va) {
  if (va % PGSIZE != 0)
    return 0;

  uint64 pa = walkaddr(pagetable, va);
  if (pa == 0)
    return 0;

  pte_t *pte = walk(pagetable, va, 0);

  if (refs.cnt[pa / PGSIZE] == 1) {
    // referenced by only one process
    *pte |= PTE_W;
    *pte &= ~PTE_C;
    return (void *)pa;
  } else {
    // referenced by multiple process
    // allocate a new page and copy to it
    char *mem = kalloc();
    if (mem == 0)
      return 0;

    // copy page to new one
    memmove(mem, (char *)pa, PGSIZE);

    // make it invalid
    *pte &= ~PTE_V;

    // add map for new page
    if (mappages(pagetable, va, PGSIZE, (uint64)mem,
                 (PTE_FLAGS(*pte) | PTE_W) & ~PTE_C) != 0) {
      kfree(mem);
      *pte |= PTE_V;
      return 0;
    }

    // decrease ref count
    kfree((char *)PGROUNDDOWN(pa));
    return mem;
  }
}