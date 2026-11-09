// pgdump            a small demo: four heap pages in different states
// pgdump <va> <n>   the page-table entries of n pages at va (hex)
//
// Debugging aid for the virtual memory lab; not graded. The kernel side
// (vmdump() in kernel/vm.c) is given, so this works before q0 is done.

#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"

static uint64
hex(char *s)
{
  uint64 v = 0;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    s += 2;
  for (; *s; s++) {
    char c = *s;
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0)
      break;
    v = v * 16 + d;
  }
  return v;
}

int
main(int argc, char **argv)
{
  if (argc == 3) {
    if (pgdump((void *)hex(argv[1]), atoi(argv[2])) < 0)
      printf("pgdump: bad range (n must be 1..64)\n");
    exit(0);
  }
  if (argc != 1) {
    printf("usage: pgdump [va n]\n");
    exit(1);
  }

  uint64 end = (uint64)sbrk(0);
  if (end % PGSIZE)
    sbrk(PGSIZE - end % PGSIZE);
  char *b = sbrk(3 * PGSIZE);  // pages 0-2: mapped
  sbrklazy(PGSIZE);            // page 3: in sz, but no page until touched
  if (b == SBRK_ERROR) {
    printf("pgdump: sbrk failed\n");
    exit(1);
  }
  checkpoint(b, 3);            // snapshot of pages 0-2 (needs q3)
  b[0] = 1;                    // page 0: written after the snapshot, so D is set
  pgprotect(b + PGSIZE, 1, 0); // page 1: read-only (needs q2)

  printf("page 0 written, page 1 made read-only, page 2 untouched, page 3 lazy\n");
  printf("flags: V valid  R read  W write  X exec  U user  A accessed  D dirty\n");
  pgdump(b, 4);
  exit(0);
}
