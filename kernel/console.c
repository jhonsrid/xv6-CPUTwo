//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "cputwo.h"
#include "defs.h"
#include "proc.h"

#define BACKSPACE 0x100  // erase the last output character
#define C(x)  ((x)-'@')  // Control-x

//
// send one character to the uart, but don't use
// interrupts or sleep(). safe to be called from
// interrupts, e.g. by printf and to echo input
// characters.
//
void
consputc(int c)
{
  if(c == BACKSPACE){
    // if the user typed backspace, overwrite with a space.
    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

#define INPUT_BUF_SIZE 128
struct spinlock cons_lock;
char cons_buf[INPUT_BUF_SIZE];
uint cons_r;  // Read index
uint cons_w;  // Write index
uint cons_e;  // Edit index

//
// user write() system calls to the console go here.
// Uses synchronous (polling) output for simplicity.
//
int
consolewrite(int user_src, uint32 src, int n)
{
  int i;

  for(i = 0; i < n; i++){
    char c;
    if(either_copyin(&c, user_src, src+i, 1) == -1)
      break;
    uartputc_sync(c);
  }

  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dst indicates whether dst is a user
// or kernel address.
//
int
consoleread(int user_dst, uint32 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons_lock);
  while(n > 0){
    // wait until interrupt handler has put some
    // input into cons_buffer.
    while(cons_r == cons_w){
      if(killed(myproc())){
        release(&cons_lock);
        return -1;
      }
      sleep(&cons_r, &cons_lock);
    }

    c = cons_buf[cons_r++ % INPUT_BUF_SIZE];

    if(c == C('D')){  // end-of-file
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        cons_r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if(c == '\n'){
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons_lock);

  return target - n;
}

//
// the console input interrupt handler.
// uartintr() calls this for each input character.
// do erase/kill processing, append to cons_buf,
// wake up consoleread() if a whole line has arrived.
//
void
consoleintr(int c)
{
  acquire(&cons_lock);

  switch(c){
  case C('P'):  // Print process list.
    procdump();
    break;
  case C('U'):  // Kill line.
    while(cons_e != cons_w &&
          cons_buf[(cons_e-1) % INPUT_BUF_SIZE] != '\n'){
      cons_e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'): // Backspace
  case '\x7f': // Delete key
    if(cons_e != cons_w){
      cons_e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    if(c != 0 && cons_e-cons_r < INPUT_BUF_SIZE){
      c = (c == '\r') ? '\n' : c;

      // echo back to the user.
      consputc(c);

      // store for consumption by consoleread().
      cons_buf[cons_e++ % INPUT_BUF_SIZE] = c;

      if(c == '\n' || c == C('D') || cons_e-cons_r == INPUT_BUF_SIZE){
        // wake up consoleread() if a whole line (or end-of-file)
        // has arrived.
        cons_w = cons_e;
        wakeup(&cons_r);
      }
    }
    break;
  }
  
  release(&cons_lock);
}

void
consoleinit(void)
{
  initlock(&cons_lock, "cons");

  uartinit();

  // connect read and write system calls
  // to consoleread and consolewrite.
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
