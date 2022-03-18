// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NULL 0
#define NBUCKET 11
#define HASH(id) (id % NBUCKET)

struct {
  struct buf buf[NBUF];
  struct {
    struct spinlock lock;
    // Linked list of all buffers, through prev/next.
    // Sorted by how recently the buffer was used.
    // head.next is most recent, head.prev is least.
    struct buf head;
  } buf_buckets[NBUCKET];
} bcache;

void binit(void) {
  struct buf *b;
  char name[8];

  for (int i = 0; i < NBUCKET; ++i) {
    snprintf(name, sizeof(name), "bcache%d", i);

    initlock(&bcache.buf_buckets[i].lock, name);

    bcache.buf_buckets[i].head.prev = &bcache.buf_buckets[i].head;
    bcache.buf_buckets[i].head.next = &bcache.buf_buckets[i].head;
  }

  // Create linked list of buffers
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    // put it on the first bucket
    b->next = bcache.buf_buckets->head.next;
    b->prev = &bcache.buf_buckets->head;

    initsleeplock(&b->lock, "buffer");

    bcache.buf_buckets->head.next->prev = b;
    bcache.buf_buckets->head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *bget(uint dev, uint blockno) {
  struct buf *b;

  int id = HASH(blockno);
  acquire(&bcache.buf_buckets[id].lock);

  // Is the block already cached?
  for (b = bcache.buf_buckets[id].head.next; b != &bcache.buf_buckets[id].head;
       b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;

      acquire(&tickslock);
      b->timestamp = ticks; // reset timestamp
      release(&tickslock);

      release(&bcache.buf_buckets[id].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  b = NULL;
  struct buf *tmp;

  // Recycle the least recently used (LRU) unused buffer.
  for (int i = id, cycle = 0; cycle != NBUCKET;
       i = (i + 1) % NBUCKET, ++cycle) {

    if (i != id && !holding(&bcache.buf_buckets[i].lock))
      acquire(&bcache.buf_buckets[i].lock);
    else
      continue;

    for (tmp = bcache.buf_buckets[i].head.next;
         tmp != &bcache.buf_buckets[i].head; tmp = tmp->next)
      if (tmp->refcnt == 0 && (b == NULL || tmp->timestamp < b->timestamp))
        b = tmp;

    if (b) {
      if (i != id) {
        b->next->prev = b->prev;
        b->prev->next = b->next;
        release(&bcache.buf_buckets[i].lock);

        b->next = bcache.buf_buckets[id].head.next;
        b->prev = &bcache.buf_buckets[id].head;

        bcache.buf_buckets[id].head.next->prev = b;
        bcache.buf_buckets[id].head.next = b;
      }

      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;

      acquire(&tickslock);
      b->timestamp = ticks;
      release(&tickslock);

      release(&bcache.buf_buckets[id].lock);
      acquiresleep(&b->lock);
      return b;
    } else if (i != id)
      release(&bcache.buf_buckets[i].lock);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *bread(uint dev, uint blockno) {
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b) {
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b) {
  if (!holdingsleep(&b->lock))
    panic("brelse");

  int lock = HASH(b->blockno);

  releasesleep(&b->lock);

  acquire(&bcache.buf_buckets[lock].lock);

  b->refcnt--;

  acquire(&tickslock);
  b->timestamp = ticks; // use timestamp rather than insert to head
  release(&tickslock);

  release(&bcache.buf_buckets[lock].lock);
}

void bpin(struct buf *b) {
  int id = HASH(b->blockno);
  acquire(&bcache.buf_buckets[id].lock);
  b->refcnt++;
  release(&bcache.buf_buckets[id].lock);
}

void bunpin(struct buf *b) {
  int id = HASH(b->blockno);
  acquire(&bcache.buf_buckets[id].lock);
  b->refcnt--;
  release(&bcache.buf_buckets[id].lock);
}
