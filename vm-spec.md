# xv6 Virtual Memory Lab

## Introduction

You are adding a save system to xv6. A game keeps its state in a few pages of memory; *save* should remember them and *load* should put them back. Copying every page on every save is wasteful: between two saves the player usually touches one or two pages. The hardware already records which pages were written (the dirty bit in each page-table entry), so the kernel can copy only those.

The lab builds that in four problems, all in `kernel/vm.c`:

| Problem | System call | In the game | Points |
| ------- | ----------- | ----------- | ------ |
| q0 | `vatopa(va)`: virtual to physical address | where does this page really live? | 10 |
| q1 | `pgdirty(va, n, &mask)`: read and clear the dirty bits | which pages changed since the last save? | 10 |
| q2 | `pgprotect(va, n, writable)`: make pages read-only or writable | anti-cheat: lock the gold page | 10 |
| q3 | `checkpoint(va, n)` / `rollback()`: an incremental snapshot of a memory region | save / load | 70 |

Do them in order: q3 uses what you learn in q0–q2. Due **2026/11/23 23:59 (UTC+8)** as a zip on eLearn (see [Submission](#submission)).

The story ends here: the sections below are the spec, and `vmcheck` and `./grade` test the system calls, not a game. `savegame` is a toy that plays with them, so you can watch your kernel work; it is not graded.

## Instructions

### Getting started

```sh
git clone https://github.com/GaGa55555LaLa/os-xv6-lab2-student.git
```

Everything is on branch `main`; commit your work there. Do not push it to a public repo (for example a fork of this one): everyone could see your code.

### Environment

Run every command inside the container in the repo (`.devcontainer/`).

1. Install Docker.
2. Build the image (once):

```sh
docker build -t xv6-lab .devcontainer
```

3. Start the container:

```sh
docker run -it --rm --name xv6 \
    -u "$(id -u):$(id -g)" \
    -v "$PWD":/xv6 -w /xv6 \
    xv6-lab bash
```

On Windows, in PowerShell:

```powershell
docker run -it --rm --name xv6 `
    -v "${PWD}:/xv6" -w /xv6 `
    xv6-lab bash
```

- Shortcut: `./lab shell` builds the image if needed and starts the container; `./lab grade` and `./lab make qemu` run one command in it (Linux, macOS, or Git Bash/WSL on Windows).
- Quit QEMU: `Ctrl-a`, then `x`. Leave the container: `exit`.
- You can also open the repo with VS Code [Dev Containers](https://code.visualstudio.com/docs/devcontainers/containers).
- Windows: if `./grade` prints `env: $'python3\r': No such file or directory`, commit your changes first, then run:

```sh
git config core.autocrlf false
git rm -r -q --cached .
git reset -q --hard
```

### Build and run

```sh
make qemu                     # 1 CPU only
$ vmcheck                      # 22 small fixed tests
$ vmcheck ckpt_incr            # one of them
$ vmtest 42                    # a random workload; 42 is the seed
$ savegame                     # the toy game (not graded); q to quit
$ pgdump                       # print a few page-table entries, flags included
```

`./grade` builds, boots xv6, runs every `vmcheck` test and prints a score out of 100.

### What the tests print

> **The output below is made up.** It only shows the format. Trust your own run.

```
test ckpt_incr: a checkpoint copies only the pages written since the last one
ev two 2
ev one 1
ev none 0
ev values 10 201 12 13 104 15 16 17
end ckpt_incr
```

Each test prints `ev` lines; `./grade` compares them with the expected ones and shows the first difference. The kernel may print `usertrap(): unexpected scause ...` in between: some tests make a child process write to a read-only page on purpose.

`pgdump [va n]` prints the page-table entries of `n` pages at `va` (hex), level by level, with each PTE's value, physical page and flags; with no arguments it shows four heap pages in different states. The same call works from your own tests (`pgdump(va, n)`), and `p` in `savegame` shows the game's pages and their snapshot. Example (made up):

```
user page table, root 0x87f25000
 L2[0] -> page-table page 0x87f21000
  L1[0] -> page-table page 0x87f20000
   L0[4]  va 0x4000  pte 0x21fc98d7  pa 0x87f26000  V R W . U A D
   L0[5]  va 0x5000  pte 0x21fd5013  pa 0x87f54000  V R . . U . .
   L0[7]  va 0x7000  pte 0x0  not mapped
shadow page table (snapshot copies), root 0x87f49000
   L0[4]  va 0x4000  pte 0x21fc9c03  pa 0x87f27000  V R . . . . .
```

`vmtest <seed>` draws a sequence of writes, `pgdirty`, `checkpoint` and `rollback` calls on a 16-page region and prints the result of each. The hidden tests use seeds you do not know; the public repo has no reference output, so check a short run against the rules by hand.

## Implementation

Fill in the six functions marked `TODO` at the end of `kernel/vm.c`. Everything around them is already there: the system call numbers and wrappers (`kernel/syscall.*`, `kernel/sysproc.c`, `user/user.h`, `user/usys.pl`), `PTE_A` / `PTE_D` in `kernel/riscv.h`, the fields `shadow`, `ckpt_va`, `ckpt_n` in `struct proc`, the calls to `ckpt_free()` in `freeproc()` and `exec()`, the range check `vmrange_ok()`, the test programs, and `pgdump` (below). `sbrklazy(n)` in the user library grows the heap without mapping pages (a page is mapped on its first access, in `vmfault()`); the tests use it to make pages that exist in `sz` but not in the page table.

### Conventions

These hold for every problem below.

- **Arguments.** `va` is a user virtual address, `n` a page count. A range is valid when `va` is page-aligned and `1 <= n <= 64` (`vmrange_ok()`); otherwise return `-1` and change nothing.
- **Mapped** means: the PTE exists and has both `PTE_V` and `PTE_U`. A lazily allocated page that was never touched, the stack guard page, the trampoline and the trapframe are *not* mapped in this sense.
- **Only user-mode stores count as writes.** The hardware sets `PTE_D` when user code stores to a page. The kernel writing into a user page (for example `read()`) does not set it, and you do not need to handle that.
- **Flush the TLB** (`sfence_vma()`) after you change any PTE.
- **Errors** return `-1`.

### q0: `uint64 vatopa(void *va)` (10 points)

*In the game: the `m` (map) command, which shows where every page really lives.*

Return the physical address that `va` maps to, **including the page offset**. Return `-1` if `va >= MAXVA` or the page is not mapped. Do not map anything: a lazy page that was never touched gives `-1`.

### q1: `int pgdirty(void *va, int n, uint64 *mask)` (10 points)

*In the game: the `d` command, "which pages changed since the last save?"*

For the `n` pages starting at `va`:

- set bit `i` of `*mask` if page `i` has `PTE_D`,
- clear `PTE_D` on those pages,
- return how many bits are set.

A page that is not mapped is clean; unmapped pages inside the range are not an error. Only the range itself is checked.

```
pages 0..3 of buf; user writes pages 1 and 3
pgdirty(buf, 4, &m)   -> 2, m = 0b1010
pgdirty(buf, 4, &m)   -> 0, m = 0        (the bits were cleared)
```

### q2: `int pgprotect(void *va, int n, int writable)` (10 points)

*In the game: the anti-cheat. The gold page is locked; `c` writes to it anyway and the process is killed.*

Make the `n` pages at `va` read-only (`writable == 0`: clear `PTE_W`) or writable again (`writable != 0`: set `PTE_W`). Return 0. If any page in the range is not mapped, return `-1` and change no page.

A later store to a read-only page is a page fault that kills the process; `usertrap()` already does this, you do not change the trap handler.

### q3: `checkpoint` / `rollback` (70 points)

*In the game: `s` (save) and `l` (load).*

An incremental snapshot of one region of the process's memory, kept by the kernel. The design is fixed: the copies live in a second page table, `p->shadow`, whose leaf PTEs point at the kernel's copies of the pages. The region is remembered in `p->ckpt_va` / `p->ckpt_n`. One snapshot per process.

```
checkpoint(buf, 4)    -> 4   first call: every page copied
user writes pages 1 and 3
checkpoint(buf, 4)    -> 2   only pages 1 and 3 copied again
user writes page 0
rollback()            -> 1   page 0 restored from its copy
rollback()            -> 0   nothing written since
```

`int checkpoint(void *va, int n)`

- The region must be mapped entirely; otherwise `-1`.
- The first call creates the shadow page table (`uvmcreate()`) and copies every page of the region. It returns `n`.
- A later call must give the same region, otherwise `-1`. It copies only the pages that have `PTE_D` set, and returns how many it copied (0 if nothing was written).
- Every call clears `PTE_D` on the whole region and flushes the TLB.
- If a page cannot be allocated, return `-1` and leave the process without a snapshot (free what was allocated).

`int rollback(void)`

- `-1` if the process has no snapshot, or a page of the region is no longer mapped (the heap shrank into it).
- Copies back, from the shadow copies, only the pages that have `PTE_D` set; clears `PTE_D` on them; flushes the TLB; returns how many it restored.
- The dirty bit is the only record of a write: a page that was written and then read by `pgdirty()` is clean, so `rollback` does not touch it.

`void ckpt_free(struct proc *p)`

- Frees the copies and the shadow page table, and resets the three fields. Already called from `freeproc()` and `exec()`.
- A process made by `fork()` starts without a snapshot (its `shadow` is 0; nothing to free).

## Hints

What you need to know, in the order you need it. Everything named here is in `kernel/vm.c` or `kernel/riscv.h` unless said otherwise.

### Toolbox

| Function / macro | What it does |
| ---------------- | ------------ |
| `walk(pt, va, alloc)` | pointer to the leaf PTE of `va`; `alloc=1` creates missing page-table pages; returns 0 if they are missing (`alloc=0`) or `kalloc()` failed |
| `PTE2PA(pte)`, `PA2PTE(pa)` | PTE → start of the physical page, and back |
| `PTE_V PTE_R PTE_W PTE_X PTE_U PTE_A PTE_D` | the flag bits of a PTE |
| `uvmcreate()` | a new, empty page table |
| `uvmunmap(pt, va, n, do_free)` | zeroes `n` leaf PTEs from `va`, frees their pages if `do_free`; skips pages that are not mapped |
| `freewalk(pt)` | frees the page-table pages; panics if a leaf is still mapped |
| `kalloc()`, `kfree(pa)` | one physical page |
| `memmove(dst, src, n)` | copy bytes; the kernel can use physical addresses directly |
| `sfence_vma()` | flush the TLB |
| `pgdump(va, n)` (user) | print the PTEs, to see what your code did |

### Page tables (q0)

- **Sv39.** A virtual address has 39 bits: three 9-bit indexes (`PX(2, va)`, `PX(1, va)`, `PX(0, va)`) and a 12-bit page offset. `walk()` follows the three levels; never dereference the 0 it returns when a level is missing.

```
va = 0x5ABC          |  L2 = 0  |  L1 = 0  |  L0 = 5  |  offset = 0xABC  |
                        9 bits     9 bits     9 bits       12 bits
                           |          |          |
satp -> root page          |          |          |
        [0] ---------------+--> L1 page          |
                                [0] -------------+--> L0 page
                                                      [5] = pte
pte  = | PPN (44 bits)              | RSW | D A G U X W R V |
         |                                  flags (low 10 bits)
         +--> PTE2PA(pte) = 0x87F27000  (start of the physical page)
              physical address = 0x87F27000 + 0xABC
```
- **PTE → physical address.** A PTE is `PPN << 10 | flags`. `PTE2PA(pte)` gives the *start* of the physical page; add the page offset, `va % PGSIZE`. Compare `walkaddr()` (no offset) with `pa0 + (dstva - va0)` in `copyout()`.
- **Who has `PTE_U`.** The trampoline, the trapframe and the stack guard page (`uvmclear()` in `exec()`) are in the page table without `PTE_U`; a lazy page that was never touched has no PTE at all. Both must give `-1`.
- **`-1` as `uint64`** is `(uint64)-1`, all bits set; the user library compares against that.

### Dirty bits and the TLB (q1)

- **Who sets the bits.** The CPU sets `PTE_D` when a user-mode store hits the page and `PTE_A` on any access (`kernel/start.c` enables this with `MENVCFG_ADUE`). The hardware never clears them; you do, with `*pte &= ~PTE_D`.
- **Why the flush.** The TLB (the CPU's cache of translations) may hold a copy of the PTE with `D` already set. Without `sfence_vma()` the CPU keeps using that copy, never writes the bit to memory again, and a later store goes unnoticed.
- **Why `read()` does not count.** `copyout()` writes through the kernel page table (`kvmmake()` maps all of RAM at its own physical address), so the user's PTE is never touched.
- **64-bit mask.** Bit `i` is `1UL << i`; `1 << i` is an `int` shift and undefined for `i >= 32`. Build the mask in your own variable; `sys_pgdirty()` in `kernel/sysproc.c` copies it out to the user.

### Permissions (q2)

- **What happens on a store.** Without `PTE_W`, a user store traps with `scause` 15 (store page fault). `usertrap()` in `kernel/trap.c` calls `vmfault()`, which returns 0 because the page is already mapped, so the process is killed with `setkilled()`. Loads still work.
- **"Change no page on error"** means two loops: the first only checks, the second only writes.

### The snapshot (q3)

```
user page table (p->pagetable)      physical pages          shadow page table (p->shadow)
 L0[5]  V R W U A D  -----------> page 0x87F27000            L0[5]  V R  ---> copy 0x87F2C000
 L0[6]  V R . U A .  -----------> page 0x87F54000            L0[6]  V R  ---> copy 0x87F2B000
 L0[7]  V R W U A .  -----------> page 0x87F53000            L0[7]  no copy yet

checkpoint: for each page with no copy or with D:  memmove(copy, page), clear D
rollback:   for each page with D:                  memmove(page, copy), clear D
```

- **The shadow page table** starts as `uvmcreate()`. `walk(p->shadow, va, 1)` gives the leaf slot for `va`, creating the levels on the way.
- **A copy** is a page from `kalloc()`, filled with `memmove()`, installed as `*spte = PA2PTE(mem) | PTE_V | PTE_R`. A leaf needs `PTE_V` and at least one of `PTE_R` / `PTE_W` / `PTE_X`, or `freewalk()` mistakes it for a page-table page and panics.
- **Copying by physical address.** The kernel page table maps all RAM 1:1, so `memmove((void *)PTE2PA(*spte), (void *)PTE2PA(*pte), PGSIZE)` copies a user page into its shadow, and the reverse restores it, whatever the user PTE's `PTE_W` says.
- **"Has a copy"** is `walk(p->shadow, va, 0)` returning a PTE with `PTE_V`. On the first call there are none, so every page is copied; later, copy a page if it has no copy or has `PTE_D`. The region is at most 64 pages, so a loop over `i` is fine.
- **Freeing** is two steps: `uvmunmap(p->shadow, va, n, 1)` frees the copies and zeroes their leaf PTEs, then `freewalk(p->shadow)` frees the page-table pages. Reset the three fields. When memory runs out halfway, call `ckpt_free()` yourself before returning `-1`: the process must not keep half a snapshot.
- **`fork()`.** `uvmcopy()` copies the PTEs with their flags, so a child may start with `PTE_D` already set; harmless, because the first `checkpoint` copies everything anyway. `p->shadow` is not copied, so the child starts without a snapshot.
- **`rollback` after `sbrk(-n)`.** The heap can shrink into the region after a `checkpoint`; check that every page is still mapped before copying.

## Trace Code

This is in the scope of the lab quiz. No report is needed.

- Address translation (`kernel/vm.c`, `kernel/riscv.h`):
  1. `walk()`: how `PX(level, va)` splits a virtual address into three indexes, what `PTE2PA` / `PA2PTE` do, and why `walk()` can return 0 or a PTE without `PTE_V`.
  2. `walkaddr()` and `copyout()`: how the kernel reaches a user page. Why does a `read()` into a user buffer **not** set that page's `PTE_D`? (Which page table does the kernel use for the store?)
  3. `mappages()`, `uvmalloc()`, `uvmunmap()`, `freewalk()`: how a page table is built and torn down.
- Lazy allocation: `sys_sbrk()` with `SBRK_LAZY` -> `usertrap()` (scause 13 / 15) -> `vmfault()` -> `mappages()`. What happens on a store to a mapped page that is not writable? Which line kills the process?
- `PTE_A` / `PTE_D`: who sets them (`kernel/start.c`: `MENVCFG_ADUE`), who clears them, and why the TLB must be flushed (`sfence_vma()`) after the kernel changes a PTE. What could go wrong with more than one CPU?
- `exec()` and `kfork()` (`uvmcopy()`): what happens to the page table, the PTE flags and the snapshot of the process.

## Rules

- Do not change these files; they are overwritten when grading: `user/` (you may add local tests there), `mkfs/`, `Makefile`, `kernel/param.h`, `kernel/syscall.h`, and the grading and environment files (`grade`, `test-xv6.py`, `.devcontainer/`, `lab`, `.gitattributes`).
- Do not add kernel source files.
- Everything runs on one CPU; do not change that.

## Grading

This xv6 project is worth **7 points** of the course grade: Implementation 3, Lab quiz 4.

### Implementation (3 points)

The four problems add up to 100 points, scaled to 3:

| Problem | Points | Public (40%) | Private (60%) |
| ------- | ------ | ------------ | ------------- |
| q0 `vatopa` | 10 | `./grade` (4 tests) | hidden tests |
| q1 `pgdirty` | 10 | `./grade` (4 tests) | hidden tests, 10 hidden seeds |
| q2 `pgprotect` | 10 | `./grade` (4 tests) | hidden tests |
| q3 `checkpoint` / `rollback` | 70 | `./grade` (10 tests) | hidden tests, 10 hidden seeds |

- The TAs use their own copies of `./grade` and the protected files listed under Rules.
- Build failure: 0 for the whole lab.
- Kernel panic or hang: 0 for the affected test or seed.
- Timeout: 15 s per test or seed (a correct run takes well under a second; `ckpt_leak` a few seconds).

**Suggestions for Testing**

Passing `./grade` does not guarantee passing the hidden tests. To improve your confidence, we encourage you to:

- Add your own tests to `user/vmcheck.c` and check the `ev` lines against the rules by hand; your changes there are overwritten when grading.
- Run `vmtest` with a few seeds (e.g., `7`, `42`) and follow a short run by hand: after a `checkpoint`, the next `rollback` returns the number of distinct pages written in between.
- Run `usertests -q`: your changes must not break the rest of xv6.

Hidden tests include 64-page ranges, regions that are partly lazy, read-only pages inside a region, and processes that fork, exec and exit with a snapshot.

### Lab quiz (4 points)

Covers the Trace Code section, and may also cover your implementation.

## Submission

1. Commit your work on `main`. The TAs grade the last commit on `main` in your zip; uncommitted changes are not graded.
2. Zip the whole `os-xv6-lab2-student` folder, including the hidden `.git` folder, as `<student_id>.zip`. Run `make clean` first to keep it small. From the folder that contains `os-xv6-lab2-student`:

```sh
zip -r <student_id>.zip os-xv6-lab2-student                 # macOS / Linux
tar -a -c -f <student_id>.zip os-xv6-lab2-student           # Windows (PowerShell)
```

3. Upload `<student_id>.zip` to eLearn.

## Deadline

**2026/11/23 23:59 (UTC+8).** Your zip must be uploaded to eLearn by then.

## Using AI tools

- You may use generative AI tools (ChatGPT, Claude, Copilot, ...) for this lab.
- You are responsible for every line you submit. The lab quiz (4 of the 7 points) asks you to trace the code and explain your implementation.
- Do not share your code with other students.
