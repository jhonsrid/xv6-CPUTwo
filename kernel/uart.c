// CPUTwo UART driver.
// Custom UART at 0x03F00000.
//
// Registers (all word-wide):
//   +0x00  Status:  bit0 = TX ready, bit1 = RX available
//   +0x04  TX:      write byte to transmit (low 8 bits)
//   +0x08  RX:      read received byte (0 if none)
//   +0x0C  Control: bit0 = RX irq enable, bit1 = TX irq enable

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "cputwo.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// for sending threads to synchronize with UART "ready" interrupts.
static struct spinlock tx_lock;
static int tx_busy;    // is the UART busy sending?
static int tx_chan;    // &tx_chan is the wait channel

extern volatile int panicking; // from printf.c
extern volatile int panicked;  // from printf.c

void
uartinit(void)
{
  // Enable RX interrupt; leave TX interrupt disabled until uartwrite() arms it.
  mmio_w(UART_CTRL, UART_CTRL_RX_IRQ);

  initlock(&tx_lock, "uart");
}

// Transmit buf[0..n-1] over the UART.
// Blocks if the UART is busy (waits for TX interrupt).
// Cannot be called from interrupt context.
void
uartwrite(char buf[], int n)
{
  acquire(&tx_lock);

  for(int i = 0; i < n; i++){
    while(tx_busy != 0){
      // Wait for a TX-complete interrupt to clear tx_busy.
      sleep(&tx_chan, &tx_lock);
    }
    mmio_w(UART_TX, (uint32)(unsigned char)buf[i]);
    tx_busy = 1;
    // Enable TX interrupt so we're notified when the byte is sent.
    mmio_w(UART_CTRL, UART_CTRL_RX_IRQ | UART_CTRL_TX_IRQ);
  }

  release(&tx_lock);
}

// Write a byte to the UART without using interrupts.
// Spins until the TX register is ready.  Used by kernel printf() and
// early-boot output before the scheduler is running.
void
uartputc_sync(int c)
{
  if(panicking == 0)
    push_off();

  if(panicked){
    for(;;)
      ;
  }

  // Spin until TX is ready.
  while((mmio_r(UART_STATUS) & UART_STATUS_TX_READY) == 0)
    ;
  mmio_w(UART_TX, (uint32)(unsigned char)c);

  if(panicking == 0)
    pop_off();
}

// Try to read one input character.
// Returns -1 if no character is waiting.
int
uartgetc(void)
{
  if(mmio_r(UART_STATUS) & UART_STATUS_RX_AVAIL){
    return (int)(mmio_r(UART_RX) & 0xFF);
  }
  return -1;
}

// Handle a UART interrupt.
// Called from devintr() for both RX and TX events.
void
uartintr(void)
{
  // Handle TX complete: wake up any thread waiting to send.
  acquire(&tx_lock);
  if((mmio_r(UART_STATUS) & UART_STATUS_TX_READY) && tx_busy){
    tx_busy = 0;
    // Disable TX interrupt until next byte is queued.
    mmio_w(UART_CTRL, UART_CTRL_RX_IRQ);
    wakeup(&tx_chan);
  }
  release(&tx_lock);

  // Drain any received characters.
  while(1){
    int c = uartgetc();
    if(c == -1)
      break;
    consoleintr(c);
  }
}
