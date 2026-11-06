// savegame: a tiny board game whose save file is a snapshot in the kernel.
//
// Not graded. It only uses the lab's system calls, so it works as far as
// your kernel does: on the skeleton, save/load/map report an error and
// the anti-cheat is off.
//
// The game state is 8 pages of heap:
//   page 0     where you are and the turn count
//   page 1     your gold; pgprotect() keeps it read-only (the anti-cheat)
//   pages 2-7  the six rooms, one page each, remembering which tiles you
//              stepped on
// A roll writes page 0 and one room page, and page 1 when you find gold,
// so a save after a few rolls copies only two or three pages.
//
//   Enter   roll the die and move; a '$' tile gives 10 gold the first time
//   s       save: checkpoint() copies the pages written since the last save
//   l       load: rollback() restores the pages written since the last save
//   d       which pages changed since the last save? pgdirty() tells, but
//           also forgets, so a later load will not undo those pages
//   m       map: the physical address of every page (vatopa)
//   p       page table: every PTE of the 8 pages, and the snapshot's (pgdump)
//   c       cheat: write the gold page directly. The anti-cheat kills you.
//   q       quit

#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"

#define NPG 8
#define ROOMS 6
#define TILES 8  // per room

static char *mem;
#define POS (*(int *)mem)
#define TURN (*(int *)(mem + 4))
#define GOLDPAGE (mem + PGSIZE)
#define GOLD (*(int *)GOLDPAGE)
#define ROOM(r) (mem + (2 + (r)) * PGSIZE)  // TILES bytes, 1 = visited

static uint seed = 2026;

static int
die(void)
{
  seed = seed * 1103515245 + 12345;
  return (seed >> 16) % 6 + 1;
}

static void
addgold(int g)
{
  pgprotect(GOLDPAGE, 1, 1);
  GOLD += g;
  pgprotect(GOLDPAGE, 1, 0);
}

static void
show(void)
{
  printf("turn %d, room %d tile %d, gold %d\n", TURN, POS / TILES, POS % TILES, GOLD);
  for (int r = 0; r < ROOMS; r++) {
    printf("  room %d [", r);
    for (int t = 0; t < TILES; t++) {
      char c = ROOM(r)[t] ? '.' : (t % 3 == 0 ? '$' : '_');
      printf("%c", POS == r * TILES + t ? '@' : c);
    }
    printf("]\n");
  }
}

static void
roll(void)
{
  int d = die();
  POS = (POS + d) % (ROOMS * TILES);
  TURN++;
  char *tile = &ROOM(POS / TILES)[POS % TILES];
  printf("rolled %d", d);
  if (!*tile && POS % TILES % 3 == 0) {
    addgold(10);
    printf(", found 10 gold");
  }
  *tile = 1;
  printf("\n");
}

static void
report(char *what, int n)
{
  if (n < 0)
    printf("%s failed: is that part of the lab done?\n", what);
  else
    printf("%s: %d page%s\n", what, n, n == 1 ? "" : "s");
}

int
main(void)
{
  char line[8];
  uint64 end = (uint64)sbrk(0);
  if (end % PGSIZE)
    sbrk(PGSIZE - end % PGSIZE);
  mem = sbrk(NPG * PGSIZE);
  if (mem == SBRK_ERROR) {
    printf("savegame: sbrk failed\n");
    exit(1);
  }
  if (pgprotect(GOLDPAGE, 1, 0) < 0)
    printf("savegame: pgprotect failed, the anti-cheat is off (q2)\n");
  printf("savegame: Enter = roll, s = save, l = load, d = changed pages, m = map, p = page table, c = cheat, q = quit\n");
  show();
  for (;;) {
    printf("> ");
    gets(line, sizeof line);
    switch (line[0]) {
    case '\n':
      roll();
      show();
      break;
    case 's':
      report("save", checkpoint(mem, NPG));
      break;
    case 'l':
      report("load", rollback());
      show();
      break;
    case 'd': {
      uint64 mask = 0;
      int n = pgdirty(mem, NPG, &mask);
      if (n < 0)
        report("pgdirty", n);
      else
        printf("%d page%s changed since the last save (mask 0x%lx); the kernel has now forgotten that\n",
               n, n == 1 ? "" : "s", mask);
      break;
    }
    case 'm':
      for (int i = 0; i < NPG; i++) {
        uint64 pa = vatopa(mem + i * PGSIZE);
        printf("  page %d: va 0x%lx -> pa ", i, (uint64)(mem + i * PGSIZE));
        if (pa == (uint64)-1)
          printf("? (vatopa failed)");
        else
          printf("0x%lx", pa);
        printf("%s\n", i == 1 ? "  (gold, read-only)" : "");
      }
      break;
    case 'p':
      pgdump(mem, NPG);
      break;
    case 'c':
      printf("cheating: writing the gold page directly...\n");
      GOLD = 9999;
      printf("no anti-cheat here: gold is now %d\n", GOLD);
      break;
    case 'q':
    case 0:  // end of input
      exit(0);
    default:
      printf("Enter, s, l, d, m, p, c or q\n");
    }
  }
}
