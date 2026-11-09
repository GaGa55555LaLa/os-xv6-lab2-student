// vmtest <seed>
//
// A random workload on a 16-page region: writes, pgdirty, checkpoint,
// rollback and checks of the page contents, in an order drawn from the
// seed. Prints one "ev" line per step; ta/vm_model.py prints the same
// lines for a correct kernel. Make up your own seeds to test with.

#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"

#define NP 16     // pages in the region
#define STEPS 80  // steps per run

static unsigned int rng;

// the same generator as ta/vm_model.py
static int
next(int n)
{
  rng = rng * 1103515245u + 12345u;
  return ((rng >> 16) & 0x7fff) % n;
}

static void
set(char *b, int i, int v)
{
  *(volatile int *)(b + i * PGSIZE) = v;
  *(volatile int *)(b + (i + 1) * PGSIZE - sizeof(int)) = v;
}

static int
get(char *b, int i)
{
  int head = *(volatile int *)(b + i * PGSIZE);
  int tail = *(volatile int *)(b + (i + 1) * PGSIZE - sizeof(int));
  return head == tail ? head : -1;
}

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(2, "usage: vmtest <seed>\n");
    exit(1);
  }
  rng = atoi(argv[1]);
  printf("vmtest: seed %s\n", argv[1]);

  uint64 end = (uint64)sbrk(0);
  if (end % PGSIZE)
    sbrk(PGSIZE - end % PGSIZE);
  char *b = sbrk(NP * PGSIZE);
  if (b == SBRK_ERROR) {
    fprintf(2, "vmtest: sbrk failed\n");
    exit(1);
  }

  int v = 0;
  for (int k = 0; k <= STEPS; k++) {
    int r = k < STEPS ? next(100) : 99; // the last step is a check
    if (r < 50) {
      int i = next(NP);
      set(b, i, ++v);
      printf("ev %d write %d %d\n", k, i, v);
    } else if (r < 62) {
      uint64 m;
      int n = pgdirty(b, NP, &m);
      printf("ev %d dirty %d 0x%lx\n", k, n, m);
    } else if (r < 78) {
      printf("ev %d checkpoint %d\n", k, checkpoint(b, NP));
    } else if (r < 92) {
      printf("ev %d rollback %d\n", k, rollback());
    } else {
      printf("ev %d check", k);
      for (int i = 0; i < NP; i++)
        printf(" %d", get(b, i));
      printf("\n");
    }
  }
  printf("vmtest: done\n");
  exit(0);
}
