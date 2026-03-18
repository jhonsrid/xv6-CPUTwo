// CPUTwo block device driver.
// Replaces the RISC-V virtio disk driver for the CPUTwo port.
//
// The block device sits at 0x03F03000:
//   +0x00  Sector   — sector number to read/write
//   +0x04  Buffer   — guest physical address (must be 512-byte aligned)
//   +0x08  Command  — 1 = read, 2 = write
//   +0x0C  Status   — 0 = idle, 1 = busy, 2 = error
//   +0x10  Control  — bit0 = IRQ enable (fires on completion)
//
// Each emulator sector is 512 bytes; BSIZE=1024 so each block = 2 sectors.

#include "types.h"
#include "cputwo.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

static struct spinlock disk_lock;
static struct buf *inflight;   // buf currently in flight, or 0

// CPUTwo: BLK_BUFFER physical address must be 512-byte aligned, and the
// emulator reads/writes exactly 512 bytes per command.  BSIZE=1024 requires
// two sector I/Os per block.  b->data sits at offset 52 in struct buf so
// it is not guaranteed aligned.  Use a 512-byte-aligned bounce buffer.
#define SECTOR_SIZE  512
#define SECTORS_PER_BLOCK  (BSIZE / SECTOR_SIZE)
static uchar bounce[BSIZE] __attribute__((aligned(SECTOR_SIZE)));

void
virtio_disk_init(void)
{
  initlock(&disk_lock, "blkdev");

  // The emulator completes I/O synchronously, so we poll for completion
  // rather than using interrupt-driven sleep/wakeup.
  mmio_w(BLK_CTRL, 0);
}

// Issue one block read or write.  Polls for completion.
// The emulator completes I/O synchronously on the BLK_CMD write,
// so the status poll always returns immediately.
void
virtio_disk_rw(struct buf *b, int write)
{
  acquire(&disk_lock);

  // Wait for the device to be idle (should already be idle).
  while(mmio_r(BLK_STATUS) == BLK_STATUS_BUSY)
    ;

  // Copy write data into the aligned bounce buffer before issuing I/O.
  if(write)
    memmove(bounce, b->data, BSIZE);

  // Issue one 512-byte sector I/O per loop iteration.
  // SECTORS_PER_BLOCK=2 for BSIZE=1024.
  for(int s = 0; s < SECTORS_PER_BLOCK; s++) {
    uint32 sector = b->blockno * SECTORS_PER_BLOCK + s;
    uint32 buf_pa = (uint32)(bounce + s * SECTOR_SIZE);

    mmio_w(BLK_SECTOR, sector);
    mmio_w(BLK_BUFFER, buf_pa);
    mmio_w(BLK_CMD, write ? BLK_CMD_WRITE : BLK_CMD_READ);

    uint32 st = mmio_r(BLK_STATUS);
    // Emulator completes synchronously; poll to confirm.
    while(st == BLK_STATUS_BUSY) {
      st = mmio_r(BLK_STATUS);
    }

    if(st == BLK_STATUS_ERR)
      panic("virtio_disk_rw: I/O error");
  }

  // Copy read data out of the bounce buffer.
  if(!write)
    memmove(b->data, bounce, BSIZE);

  release(&disk_lock);
}

// Called from devintr() when a block-device interrupt fires.
void
virtio_disk_intr(void)
{
  acquire(&disk_lock);

  if(inflight != 0){
    wakeup(inflight);
  }

  release(&disk_lock);
}
