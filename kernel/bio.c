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
#include "cputwo.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct spinlock bcache_lock;
struct buf bcache_buf[NBUF];
// Linked list of all buffers, through prev/next.
// Sorted by how recently the buffer was used.
// head.next is most recent, head.prev is least.
struct buf bcache_head;
// CPUTwo TCC bug: global struct fields after embedded struct use addend=0.
// Use pointer access (bhead->next, bhead->prev) instead of bcache_head.next/prev.
struct buf *bhead = &bcache_head;

void
binit(void)
{
  struct buf *b;
  int _n = NBUF;

  initlock(&bcache_lock, "bcache");

  // Create linked list of buffers
  bhead->prev = bhead;
  bhead->next = bhead;
  for(b = bcache_buf; b < bcache_buf+_n; b++){
    b->next = bhead->next;
    b->prev = bhead;
    initsleeplock(&b->lock, "buffer");
    bhead->next->prev = b;
    bhead->next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache_lock);

  // Is the block already cached?
  for(b = bhead->next; b != bhead; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bhead->prev; b != bhead; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache_lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bhead->next;
    b->prev = bhead;
    bhead->next->prev = b;
    bhead->next = b;
  }
  
  release(&bcache_lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache_lock);
  b->refcnt++;
  release(&bcache_lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache_lock);
  b->refcnt--;
  release(&bcache_lock);
}


