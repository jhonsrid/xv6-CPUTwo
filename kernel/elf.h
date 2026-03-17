// Format of an ELF32 executable file (CPUTwo uses 32-bit ELF)

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// ELF32 file header (52 bytes)
struct elfhdr {
  uint   magic;       // must equal ELF_MAGIC
  uchar  elf[12];     // e_ident[4..15]: class, data, version, OS/ABI, padding
  ushort type;        // ET_EXEC = 2
  ushort machine;     // EM_CPUTWO = 0x9002
  uint   version;     // EV_CURRENT = 1
  uint32 entry;       // virtual address of entry point
  uint32 phoff;       // offset of program header table
  uint32 shoff;       // offset of section header table
  uint   flags;
  ushort ehsize;      // size of this header (52)
  ushort phentsize;   // size of a program header (32)
  ushort phnum;       // number of program headers
  ushort shentsize;
  ushort shnum;
  ushort shstrndx;
};

// ELF32 program section header (32 bytes)
// NOTE: ELF32 and ELF64 have different field order — flags comes last in ELF32.
struct proghdr {
  uint32 type;
  uint32 off;         // file offset
  uint32 vaddr;       // virtual address
  uint32 paddr;       // physical address
  uint32 filesz;      // size in file
  uint32 memsz;       // size in memory
  uint32 flags;       // PF_X/PF_W/PF_R (flags after sizes in ELF32)
  uint32 align;
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
