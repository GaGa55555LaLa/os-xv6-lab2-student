// vmcheck [test]
//
// Public tests for the virtual memory lab, one per name. Each test runs
// in its own child process, so that a snapshot or a killed process in
// one test cannot affect the next. Every test prints "ev ..." lines;
// grade compares them with the expected ones.

#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
#include "user/user.h"

#define EV(fmt, ...) printf("ev " fmt "\n", ##__VA_ARGS__)
#define NOPA ((uint64)-1)

// n fresh pages from sbrk, page aligned, with every byte zero.
static char *
pages(int n)
{
  uint64 end = (uint64)sbrk(0);
  if (end % PGSIZE)
    sbrk(PGSIZE - end % PGSIZE);
  char *p = sbrk(n * PGSIZE);
  if (p == SBRK_ERROR) {
    printf("vmcheck: sbrk failed\n");
    exit(1);
  }
  return p;
}

// like pages(), but lazily: no page is mapped until it is touched.
static char *
lazypages(int n)
{
  uint64 end = (uint64)sbrk(0);
  if (end % PGSIZE)
    sbrk(PGSIZE - end % PGSIZE);
  char *p = sbrklazy(n * PGSIZE);
  if (p == SBRK_ERROR) {
    printf("vmcheck: sbrklazy failed\n");
    exit(1);
  }
  return p;
}

// Page i of a region holds one int at its start and a copy at its end,
// so that a half-copied page shows up.
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

static void
fill(char *b, int n, int base)
{
  for (int i = 0; i < n; i++)
    set(b, i, base + i);
}

static void
values(char *b, int n)
{
  printf("ev values");
  for (int i = 0; i < n; i++)
    printf(" %d", get(b, i));
  printf("\n");
}

static int
run_child(void (*fn)(void *), void *arg)
{
  int pid = fork();
  if (pid < 0) {
    printf("vmcheck: fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    fn(arg);
    exit(0);
  }
  int status;
  wait(&status);
  return status;
}

// --- q0: vatopa ----------------------------------------------------------

static void
pa_offset(void)
{
  char *b = pages(1);
  uint64 pa = vatopa(b), pa1 = vatopa(b + 100), pa2 = vatopa(b + PGSIZE - 1);
  EV("mapped %d", pa != NOPA);
  EV("aligned %d", pa % PGSIZE == 0);
  EV("offset %d %d", pa1 - pa == 100, pa2 - pa == PGSIZE - 1);
  EV("ram %d", pa >= KERNBASE && pa < PHYSTOP);
}

static void
pa_lazy(void)
{
  volatile char *b = lazypages(2);
  EV("untouched %ld", (long)vatopa((void *)b));
  b[0] = 1;
  EV("touched %d", vatopa((void *)b) != NOPA);
  EV("next_untouched %ld", (long)vatopa((void *)(b + PGSIZE)));
  int zero = b[PGSIZE] == 0; // a load faults the page in too
  EV("read_faults_in %d %d", zero, vatopa((void *)(b + PGSIZE)) != NOPA);
}

static void
pa_guard(void)
{
  int x;
  uint64 stack = PGROUNDDOWN((uint64)&x);
  EV("stack %d", vatopa(&x) != NOPA);
  EV("guard %ld", (long)vatopa((void *)(stack - PGSIZE)));
  EV("end %ld", (long)vatopa(sbrk(0)));
  EV("trampoline %ld", (long)vatopa((void *)TRAMPOLINE));
  EV("maxva %ld", (long)vatopa((void *)MAXVA));
}

static void
pa_fork_child(void *arg)
{
  char *b = arg;
  uint64 pa = vatopa(b + PGSIZE); // the parent left its answer in page 1
  EV("child_copy %d %d", b[0] == 7, vatopa(b) != *(uint64 *)(b + PGSIZE));
  EV("child_page1 %d", pa != NOPA);
}

static void
pa_fork(void)
{
  char *b = pages(2);
  b[0] = 7;
  uint64 pa = vatopa(b);
  *(uint64 *)(b + PGSIZE) = pa;
  run_child(pa_fork_child, b);
  EV("parent_same %d", vatopa(b) == pa);
}

// --- q1: pgdirty ---------------------------------------------------------

static void
dirty_mask(void)
{
  volatile char *b = pages(8);
  uint64 m;
  int r = pgdirty((void *)b, 8, &m);
  EV("fresh %d 0x%lx", r, m);
  b[0] = 1;
  b[2 * PGSIZE] = 1;
  b[5 * PGSIZE + 123] = 1;
  r = pgdirty((void *)b, 8, &m);
  EV("written %d 0x%lx", r, m);
}

static void
dirty_clear(void)
{
  volatile char *b = pages(4);
  uint64 m;
  int sum = 0;
  b[0] = 1;
  b[PGSIZE] = 2;
  int r = pgdirty((void *)b, 4, &m);
  EV("first %d 0x%lx", r, m);
  r = pgdirty((void *)b, 4, &m);
  EV("second %d 0x%lx", r, m);
  for (int i = 0; i < 4; i++)
    sum += b[i * PGSIZE];
  r = pgdirty((void *)b, 4, &m);
  EV("after_read %d 0x%lx %d", r, m, sum);
}

static void
dirty_again(void)
{
  volatile char *b = pages(2);
  uint64 m;
  b[PGSIZE] = 1;
  int r = pgdirty((void *)b, 2, &m);
  EV("first %d 0x%lx", r, m);
  b[PGSIZE] = 2;
  r = pgdirty((void *)b, 2, &m);
  EV("again %d 0x%lx", r, m);
  b[PGSIZE] = 3;
  r = pgdirty((void *)b, 1, &m);
  EV("outside %d 0x%lx", r, m);
  r = pgdirty((void *)b, 2, &m);
  EV("still %d 0x%lx", r, m);
}

static void
dirty_args(void)
{
  volatile char *b = pages(2);
  uint64 m;
  EV("zero %d", pgdirty((void *)b, 0, &m));
  EV("many %d", pgdirty((void *)b, 65, &m));
  EV("unaligned %d", pgdirty((void *)(b + 1), 1, &m));
  EV("maxva %d", pgdirty((void *)MAXVA, 1, &m));
  volatile char *l = lazypages(4);
  l[PGSIZE] = 1;
  int r = pgdirty((void *)l, 4, &m);
  EV("lazy %d 0x%lx", r, m);
  r = pgdirty(sbrk(0), 1, &m);
  EV("beyond %d 0x%lx", r, m);
}

// --- q2: pgprotect -------------------------------------------------------

static void
write_page0(void *arg)
{
  volatile char *b = arg;
  b[0] = 2;
  EV("child_wrote");
}

static void
prot_kill(void)
{
  volatile char *b = pages(1);
  b[0] = 1;
  EV("protect %d", pgprotect((void *)b, 1, 0));
  EV("killed %d", run_child(write_page0, (void *)b));
  EV("value %d", b[0]);
}

static void
prot_read(void)
{
  volatile char *b = pages(2);
  uint64 m;
  b[0] = 5;
  b[PGSIZE] = 6;
  pgprotect((void *)b, 2, 0);
  EV("read %d %d", b[0], b[PGSIZE]);
  EV("mapped %d", vatopa((void *)b) != NOPA);
  int r = pgdirty((void *)b, 2, &m);
  EV("dirty %d 0x%lx", r, m);
}

static void
prot_restore(void)
{
  volatile char *b = pages(1);
  pgprotect((void *)b, 1, 0);
  EV("rw %d", pgprotect((void *)b, 1, 1));
  EV("child %d", run_child(write_page0, (void *)b));
  b[0] = 3;
  EV("value %d", b[0]);
}

static void
prot_unmapped(void)
{
  volatile char *l = lazypages(2);
  l[0] = 1;
  EV("partial %d", pgprotect((void *)l, 2, 0));
  EV("unchanged %d", run_child(write_page0, (void *)l));
  EV("lazy %d", pgprotect((void *)(l + PGSIZE), 1, 0));
  EV("zero %d", pgprotect((void *)l, 0, 0));
  EV("beyond %d", pgprotect(sbrk(0), 1, 0));
  EV("one %d", pgprotect((void *)l, 1, 0));
}

// --- q3: checkpoint / rollback -------------------------------------------

static void
ckpt_first(void)
{
  char *b = pages(8);
  fill(b, 8, 10);
  EV("first %d", checkpoint(b, 8));
  EV("again %d", checkpoint(b, 8));
  values(b, 8);
}

static void
ckpt_incr(void)
{
  char *b = pages(8);
  fill(b, 8, 10);
  checkpoint(b, 8);
  set(b, 1, 101);
  set(b, 4, 104);
  EV("two %d", checkpoint(b, 8));
  set(b, 1, 201);
  EV("one %d", checkpoint(b, 8));
  EV("none %d", checkpoint(b, 8));
  values(b, 8);
}

static void
ckpt_rollback(void)
{
  char *b = pages(8);
  fill(b, 8, 10);
  EV("take %d", checkpoint(b, 8));
  set(b, 2, 99);
  set(b, 3, 99);
  set(b, 7, 99);
  EV("rollback %d", rollback());
  values(b, 8);
}

static void
ckpt_twice(void)
{
  char *b = pages(4);
  fill(b, 4, 10);
  checkpoint(b, 4);
  set(b, 0, 50);
  EV("first %d", rollback());
  EV("second %d", rollback());
  set(b, 3, 60);
  EV("third %d", rollback());
  values(b, 4);
}

static void
ckpt_clean(void)
{
  char *b = pages(4);
  fill(b, 4, 10);
  checkpoint(b, 4);
  set(b, 1, 70);
  EV("rollback %d", rollback());
  EV("checkpoint %d", checkpoint(b, 4));
  set(b, 2, 80);
  EV("refresh %d", checkpoint(b, 4));
  EV("nothing %d", rollback());
  values(b, 4);
}

static void
ckpt_pgdirty(void)
{
  char *b = pages(4);
  uint64 m;
  fill(b, 4, 10);
  checkpoint(b, 4);
  set(b, 0, 90);
  int r = pgdirty(b, 4, &m);
  EV("dirty %d 0x%lx", r, m);
  EV("rollback %d", rollback());
  values(b, 4);
  set(b, 1, 91);
  EV("checkpoint %d", checkpoint(b, 4));
  set(b, 0, 92);
  EV("rollback2 %d", rollback());
  values(b, 4);
}

static void
ckpt_fork_child(void *arg)
{
  char *b = arg;
  EV("child_rollback %d", rollback());
  EV("child_checkpoint %d", checkpoint(b, 4));
  set(b, 1, 66);
  EV("child_rollback2 %d", rollback());
  values(b, 4);
}

static void
ckpt_fork(void)
{
  char *b = pages(4);
  fill(b, 4, 10);
  checkpoint(b, 4);
  set(b, 0, 55);
  run_child(ckpt_fork_child, b);
  EV("parent_rollback %d", rollback());
  values(b, 4);
}

static void
ckpt_exec_child(void *arg)
{
  char *argv[] = { "vmcheck", "_exec", 0 };
  // fork does not inherit a snapshot. The process calling exec must own one.
  if (checkpoint(arg, 4) != 4) {
    EV("child_checkpoint_failed");
    exit(1);
  }
  exec("vmcheck", argv);
  EV("exec_failed");
}

static void
ckpt_exec(void)
{
  char *b = pages(4);
  fill(b, 4, 10);
  EV("take %d", checkpoint(b, 4));
  EV("child %d", run_child(ckpt_exec_child, b));
}

static void
ckpt_leak_child(void *arg)
{
  char *r = sbrk(64 * PGSIZE);
  if (r == SBRK_ERROR || checkpoint(r, 64) != 64)
    exit(1);
}

static void
ckpt_leak(void)
{
  // 600 x (64 pages + the shadow page table) is more than the 128 MB of
  // RAM: this only passes if exit() frees the snapshot.
  for (int i = 0; i < 600; i++) {
    if (run_child(ckpt_leak_child, 0) != 0) {
      EV("child_failed %d", i);
      break;
    }
  }
  char *r = sbrk(64 * PGSIZE);
  EV("alloc %d", r != SBRK_ERROR);
  EV("checkpoint %d", r == SBRK_ERROR ? -1 : checkpoint(r, 64));
}

static void
ckpt_args(void)
{
  char *b = pages(4);
  EV("unaligned %d", checkpoint(b + 1, 4));
  EV("zero %d", checkpoint(b, 0));
  EV("many %d", checkpoint(b, 65));
  char *l = lazypages(2);
  EV("lazy %d", checkpoint(l, 2));
  EV("no_snapshot %d", rollback());
  EV("take %d", checkpoint(b, 4));
  EV("other %d", checkpoint(b, 3));
  EV("other2 %d", checkpoint(b + PGSIZE, 3));
  EV("same %d", checkpoint(b, 4));
  sbrk(-3 * PGSIZE); // the 2 lazy pages and page 3 of the region
  EV("shrunk %d", rollback());
  EV("shrunk_checkpoint %d", checkpoint(b, 4));
}

// -------------------------------------------------------------------------

struct test {
  const char *name;
  const char *what;
  void (*fn)(void);
};

static struct test tests[] = {
  // q0
  { "pa_offset", "vatopa keeps the page offset and returns an address in RAM", pa_offset },
  { "pa_lazy", "a lazily allocated page has no physical address until it is touched", pa_lazy },
  { "pa_guard", "the guard page, the end of the heap, the trampoline and MAXVA are not user addresses", pa_guard },
  { "pa_fork", "after fork, the same virtual address maps to a different physical page in the child", pa_fork },
  // q1
  { "dirty_mask", "pgdirty reports the pages written to since allocation", dirty_mask },
  { "dirty_clear", "pgdirty clears the dirty bits; reading does not set them", dirty_clear },
  { "dirty_again", "a write after pgdirty sets the dirty bit again (TLB flushed)", dirty_again },
  { "dirty_args", "bad ranges return -1; unmapped pages are clean", dirty_args },
  // q2
  { "prot_kill", "a write to a read-only page kills the process", prot_kill },
  { "prot_read", "a read-only page can still be read; its dirty bit is kept", prot_read },
  { "prot_restore", "pgprotect(..., 1) makes the page writable again", prot_restore },
  { "prot_unmapped", "an unmapped page in the range: -1 and nothing changes", prot_unmapped },
  // q3
  { "ckpt_first", "the first checkpoint copies every page, the next one nothing", ckpt_first },
  { "ckpt_incr", "a checkpoint copies only the pages written since the last one", ckpt_incr },
  { "ckpt_rollback", "rollback restores the modified pages and counts them", ckpt_rollback },
  { "ckpt_twice", "a second rollback has nothing to restore", ckpt_twice },
  { "ckpt_clean", "rollback leaves the pages clean; a checkpoint keeps new values", ckpt_clean },
  { "ckpt_pgdirty", "the dirty bit is the only record: pgdirty hides a change from rollback", ckpt_pgdirty },
  { "ckpt_fork", "a child starts without a snapshot; the parent keeps its own", ckpt_fork },
  { "ckpt_exec", "exec drops the snapshot", ckpt_exec },
  { "ckpt_leak", "exit frees the snapshot", ckpt_leak },
  { "ckpt_args", "bad ranges, a different region and an unmapped region return -1", ckpt_args },
};

#define NTESTS (sizeof(tests) / sizeof(tests[0]))

static void
runtest(void *arg)
{
  struct test *t = arg;
  t->fn();
}

int
main(int argc, char *argv[])
{
  if (argc == 2 && strcmp(argv[1], "_exec") == 0) {
    // the image exec'd by ckpt_exec
    // Re-map the old heap range: otherwise a stale snapshot could also
    // return -1 simply because its pages are absent in this new image.
    char *b = pages(4);
    fill(b, 4, 100);
    EV("after_exec %d", rollback());
    exit(0);
  }
  for (int i = 0; i < NTESTS; i++) {
    if (argc >= 2 && strcmp(argv[1], tests[i].name) != 0)
      continue;
    printf("test %s: %s\n", tests[i].name, tests[i].what);
    run_child(runtest, &tests[i]);
    printf("end %s\n", tests[i].name);
  }
  exit(0);
}
