K=kernel
U=user

OBJS = \
  $K/entry.o \
  $K/start.o \
  $K/console.o \
  $K/printf.o \
  $K/uart.o \
  $K/kalloc.o \
  $K/spinlock.o \
  $K/string.o \
  $K/main.o \
  $K/vm.o \
  $K/proc.o \
  $K/swtch.o \
  $K/trampoline.o \
  $K/trap.o \
  $K/syscall.o \
  $K/sysproc.o \
  $K/bio.o \
  $K/fs.o \
  $K/log.o \
  $K/sleeplock.o \
  $K/file.o \
  $K/pipe.o \
  $K/exec.o \
  $K/sysfile.o \
  $K/kernelvec.o \
  $K/plic.o \
  $K/virtio_disk.o

TCC = /Users/john/tinycc_CPUTwo/cputwo-tcc
EMU = /Users/john/CPUTwo/build/emulatortwo

CC = $(TCC)
LD = $(TCC)

CFLAGS  = -ffreestanding -fno-common -nostdlib -fno-builtin
CFLAGS += -DTCC_TARGET_CPUTWO
CFLAGS += -B/Users/john/tinycc_CPUTwo
CFLAGS += -I.

# No LDFLAGS needed for TCC (no -z max-page-size)
LDFLAGS =

$K/kernel: $(OBJS)
	$(LD) -B/Users/john/tinycc_CPUTwo -nostdlib -static $(LDFLAGS) -Wl,-Ttext=0x1000 -o $K/kernel $(OBJS)

# Compile .S assembly files
$K/%.o: $K/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile .c files
$K/%.o: $K/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

tags: $(OBJS)
	etags kernel/*.S kernel/*.c

ULIB = $U/ulib.o $U/usys.o $U/printf.o $U/umalloc.o

_%: %.o $(ULIB)
	$(LD) -B/Users/john/tinycc_CPUTwo -nostdlib -static $(LDFLAGS) -Wl,-Ttext=0x0 -Wl,-e,start -o $@ $< $(ULIB)

$U/usys.S: $U/usys.pl
	perl $U/usys.pl > $U/usys.S

$U/usys.o: $U/usys.S
	$(CC) $(CFLAGS) -c -o $U/usys.o $U/usys.S

$U/_forktest: $U/forktest.o $(ULIB)
	$(LD) -B/Users/john/tinycc_CPUTwo -nostdlib -static $(LDFLAGS) -Wl,-Ttext=0x0 -Wl,-e,start -o $U/_forktest $U/forktest.o $U/ulib.o $U/usys.o

# Compile user .c files
$U/%.o: $U/%.c
	$(CC) $(CFLAGS) -I$U -c -o $@ $<

# Compile user .S files
$U/%.o: $U/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

mkfs/mkfs: mkfs/mkfs.c $K/fs.h $K/param.h
	gcc -Wno-unknown-attributes -I. -o mkfs/mkfs mkfs/mkfs.c

# Prevent deletion of intermediate files
.PRECIOUS: %.o

UPROGS=\
	$U/_cat\
	$U/_echo\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_grind\
	$U/_wc\
	$U/_zombie\
	$U/_logstress\
	$U/_forphan\
	$U/_dorphan\

fs.img: mkfs/mkfs README $(UPROGS)
	mkfs/mkfs fs.img README $(UPROGS)

-include kernel/*.d user/*.d

clean:
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*/*.o */*.d */*.asm */*.sym \
	$K/kernel fs.img \
	mkfs/mkfs .gdbinit \
	$U/usys.S \
	$(UPROGS)

run: $K/kernel fs.img
	$(EMU) -blk fs.img $K/kernel

run-debug: $K/kernel fs.img
	$(EMU) -d -blk fs.img $K/kernel
