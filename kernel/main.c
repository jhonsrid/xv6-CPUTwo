#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "cputwo.h"
#include "defs.h"

volatile static int started = 0;

// start() in start.c jumps here in supervisor mode on all CPUs.
void
main(void)
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    printf("kinit...\n");
    kinit();           // physical page allocator
    printf("kvminit...\n");
    kvminit();         // create kernel page table
    printf("kvminithart...\n");
    kvminithart();     // turn on paging
    printf("procinit...\n");
    procinit();        // process table
    printf("trapinit...\n");
    trapinit();        // trap vectors
    printf("trapinithart...\n");
    trapinithart();    // install kernel trap vector
    printf("plicinit...\n");
    plicinit();        // set up interrupt controller
    printf("plicinithart...\n");
    plicinithart();    // ask IC for device interrupts
    printf("timerinit...\n");
    timerinit();       // start the periodic timer
    printf("binit...\n");
    binit();           // buffer cache
    printf("iinit...\n");
    iinit();           // inode table
    printf("fileinit...\n");
    fileinit();        // file table
    printf("virtio_disk_init...\n");
    virtio_disk_init(); // block device
    printf("userinit...\n");
    userinit();        // first user process
    printf("userinit done, calling scheduler\n");
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();     // turn on paging
    trapinithart();    // install kernel trap vector
    plicinithart();    // ask IC for device interrupts
  }

  scheduler();
}
