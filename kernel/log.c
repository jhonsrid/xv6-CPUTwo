#include "types.h"
#include "cputwo.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
  int n;
  int block[LOGBLOCKS];
};

struct spinlock log_lock;
int log_start;
int log_outstanding; // how many FS sys calls are executing.
int log_committing;  // in commit(), please wait.
int log_dev;
struct logheader log_lh;

static void recover_from_log(void);
static void commit();

void
initlog(int dev, struct superblock *sb)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  initlock(&log_lock, "log");
  log_start = sb->logstart;
  log_dev = dev;
  recover_from_log();
}

// Copy committed blocks from log to their home location
static void
install_trans(int recovering)
{
  int tail;

  for (tail = 0; tail < log_lh.n; tail++) {
    if(recovering) {
      printf("recovering tail %d dst %d\n", tail, log_lh.block[tail]);
    }
    struct buf *lbuf = bread(log_dev, log_start+tail+1); // read log block
    struct buf *dbuf = bread(log_dev, log_lh.block[tail]); // read dst
    memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst
    bwrite(dbuf);  // write dst to disk
    if(recovering == 0)
      bunpin(dbuf);
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
static void
read_head(void)
{
  struct buf *buf = bread(log_dev, log_start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  log_lh.n = lh->n;
  for (i = 0; i < log_lh.n; i++) {
    log_lh.block[i] = lh->block[i];
  }
  brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
static void
write_head(void)
{
  struct buf *buf = bread(log_dev, log_start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log_lh.n;
  for (i = 0; i < log_lh.n; i++) {
    hb->block[i] = log_lh.block[i];
  }
  bwrite(buf);
  brelse(buf);
}

static void
recover_from_log(void)
{
  read_head();
  install_trans(1); // if committed, copy from log to disk
  log_lh.n = 0;
  write_head(); // clear the log
}

// called at the start of each FS system call.
void
begin_op(void)
{
  acquire(&log_lock);
  while(1){
    if(log_committing){
      sleep(&log_lock, &log_lock);
    } else if(log_lh.n + (log_outstanding+1)*MAXOPBLOCKS > LOGBLOCKS){
      // this op might exhaust log space; wait for commit.
      sleep(&log_lock, &log_lock);
    } else {
      log_outstanding += 1;
      release(&log_lock);
      break;
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log_lock);
  log_outstanding -= 1;
  if(log_committing)
    panic("log_committing");
  if(log_outstanding == 0){
    do_commit = 1;
    log_committing = 1;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log_outstanding has decreased
    // the amount of reserved space.
    wakeup(&log_lock);
  }
  release(&log_lock);

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log_lock);
    log_committing = 0;
    wakeup(&log_lock);
    release(&log_lock);
  }
}

// Copy modified blocks from cache to log.
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log_lh.n; tail++) {
    struct buf *to = bread(log_dev, log_start+tail+1); // log block
    struct buf *from = bread(log_dev, log_lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // write the log
    brelse(from);
    brelse(to);
  }
}

static void
commit()
{
  if (log_lh.n > 0) {
    write_log();     // Write modified blocks from cache to log
    write_head();    // Write header to disk -- the real commit
    install_trans(0); // Now install writes to home locations
    log_lh.n = 0;
    write_head();    // Erase the transaction from the log
  }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b)
{
  int i;

  acquire(&log_lock);
  if (log_lh.n >= LOGBLOCKS)
    panic("too big a transaction");
  if (log_outstanding < 1)
    panic("log_write outside of trans");

  for (i = 0; i < log_lh.n; i++) {
    if (log_lh.block[i] == b->blockno)   // log absorption
      break;
  }
  log_lh.block[i] = b->blockno;
  if (i == log_lh.n) {  // Add new block to log?
    bpin(b);
    log_lh.n++;
  }
  release(&log_lock);
}

