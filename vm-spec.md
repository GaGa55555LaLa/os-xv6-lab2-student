# xv6 Virtual Memory Lab

## Introduction

You are adding a save system to xv6. A game keeps its state in a few pages; *save* remembers them, *load* puts them back. Copying every page on every save is wasteful: between two saves the player touches one or two. The hardware already marks the pages that were written (the dirty bit in each page-table entry), so the kernel can copy only those.

Four problems, all in `kernel/vm.c`:

| Problem | System call | In the game | Points |
| ------- | ----------- | ----------- | ------ |
| q0 | `vatopa(va)`: virtual to physical address | where does this page really live? | 10 |
| q1 | `pgdirty(va, n, &mask)`: read and clear the dirty bits | which pages changed since the last save? | 10 |
| q2 | `pgprotect(va, n, writable)`: make pages read-only or writable | anti-cheat: lock the gold page | 10 |
| q3 | `checkpoint(va, n)` / `rollback()`: incremental snapshot of a memory region | save / load | 70 |

Do them in order: q3 uses q0–q2. Due **2026/11/23 23:59 (UTC+8)**, as a zip on eLearn (see [Submission](#submission)).

The story ends here. `./grade` tests the system calls, not a game; `savegame` is a toy that uses them and is not graded.

## Instructions

### Getting started

```sh
git clone https://github.com/GaGa55555LaLa/os-xv6-lab2-student.git
```

Everything is on branch `main`; commit your work there. Do not push it to a public repo: everyone could see your code.

### Environment

Run every command inside the container (`.devcontainer/`).

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

- Shortcut: `./lab shell` builds the image if needed and starts the container; `./lab grade` and `./lab make qemu` run one command in it (Linux, macOS, Git Bash/WSL).
- Quit QEMU: `Ctrl-a`, then `x`. Leave the container: `exit`.
- VS Code [Dev Containers](https://code.visualstudio.com/docs/devcontainers/containers) also works.
- Windows: if `./grade` prints `env: $'python3\r': No such file or directory`, commit first, then:

```sh
git config core.autocrlf false
git rm -r -q --cached .
git reset -q --hard
```

### Build and run

```sh
make qemu                     # 1 CPU only
$ vmcheck                      # the 22 public tests
$ vmcheck ckpt_incr            # one of them
$ vmtest 42                    # random workload, seed 42
$ pgdump                       # a few page-table entries, with flags
$ savegame                     # the toy game; q to quit
```

`./grade` runs every `vmcheck` test and prints a score out of 100.

### What the tests print

> **Made-up output.** It only shows the format.

```
test ckpt_incr: a checkpoint copies only the pages written since the last one
ev two 2
ev one 1
ev none 0
ev values 10 201 12 13 104 15 16 17
end ckpt_incr
```

`./grade` compares the `ev` lines with the expected ones and shows the first difference. `usertrap(): unexpected scause ...` in between is normal: some tests write to a read-only page on purpose.

`vmtest <seed>` runs a random sequence of writes, `pgdirty`, `checkpoint` and `rollback` on 16 pages and prints each result. There is no reference output; check a short run against the rules by hand.

`pgdump [va n]` prints the page table for `n` pages at `va` (hex), level by level, each PTE with its physical page and flags; without arguments, four heap pages in different states. Call `pgdump(va, n)` from your own tests, or press `p` in `savegame`.

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

## Implementation

Fill in the six functions marked `TODO` at the end of `kernel/vm.c`. Everything else is in place: the system calls, `PTE_A` / `PTE_D`, the snapshot fields `shadow`, `ckpt_va`, `ckpt_n` in `struct proc`, the calls to `ckpt_free()` in `freeproc()` and `exec()`, `vmrange_ok()`, and the tests. The tests use `sbrklazy(n)`, which grows the heap without mapping pages (a page is mapped on first access, in `vmfault()`).

### Conventions

- **Range.** `va` must be page-aligned and `1 <= n <= 64` (`vmrange_ok()`); otherwise return `-1` and change nothing.
- **Mapped** = the PTE exists and has `PTE_V` and `PTE_U`. An untouched lazy page, the stack guard page, the trampoline and the trapframe are *not* mapped.
- **Only user-mode stores count as writes.** The kernel writing into a user page (`read()`) does not set `PTE_D`; you need not handle it.
- **Flush the TLB** (`sfence_vma()`) after changing any PTE.
- **Errors** return `-1`.

### q0: `uint64 vatopa(void *va)` (10 points)

*In the game: `m`, the map.*

The physical address of `va`, **page offset included**; `-1` if `va >= MAXVA` or the page is not mapped. Do not map anything: an untouched lazy page gives `-1`.

### q1: `int pgdirty(void *va, int n, uint64 *mask)` (10 points)

*In the game: `d`, which pages changed since the last save.*

For the `n` pages at `va`: set bit `i` of `*mask` if page `i` has `PTE_D`, clear those bits, return how many were set. An unmapped page is clean, not an error.

```
user writes pages 1 and 3 of buf
pgdirty(buf, 4, &m)   -> 2, m = 0b1010
pgdirty(buf, 4, &m)   -> 0, m = 0        (cleared)
```

### q2: `int pgprotect(void *va, int n, int writable)` (10 points)

*In the game: the anti-cheat. `c` writes to the locked gold page and the process is killed.*

Clear `PTE_W` on the `n` pages at `va` (`writable == 0`) or set it (`writable != 0`); return 0. If any page is not mapped, return `-1` and change no page. A store to a read-only page then kills the process; `usertrap()` already does that.

### q3: `checkpoint` / `rollback` (70 points)

*In the game: `s` save, `l` load.*

An incremental snapshot of one region, kept by the kernel in a second page table, `p->shadow`, whose leaves point at copies of the pages. The region is kept in `p->ckpt_va` / `p->ckpt_n`. One snapshot per process.

```
checkpoint(buf, 4)    -> 4   first call: every page copied
user writes pages 1 and 3
checkpoint(buf, 4)    -> 2   only pages 1 and 3 copied again
user writes page 0
rollback()            -> 1   page 0 restored
rollback()            -> 0   nothing written since
```

`int checkpoint(void *va, int n)`

- The whole region must be mapped; otherwise `-1`.
- First call: create the shadow page table (`uvmcreate()`), copy every page, return `n`.
- Later calls: same region or `-1`; copy only the pages with `PTE_D`; return how many (0 if none).
- Every call clears `PTE_D` on the region and flushes the TLB.
- Out of memory: free what was allocated, leave the process without a snapshot, return `-1`.

`int rollback(void)`

- `-1` if there is no snapshot, or a page of the region is no longer mapped (the heap shrank).
- Copy back only the pages with `PTE_D`, clear `PTE_D` on them, flush the TLB, return how many.
- The dirty bit is the only record: a page cleared by `pgdirty()` is not restored.

`void ckpt_free(struct proc *p)`

- Free the copies and the shadow page table; reset the three fields. Called from `freeproc()` and `exec()`.
- A `fork()` child starts with `shadow == 0`: nothing to free.

## Hints

In `kernel/vm.c` and `kernel/riscv.h` unless said otherwise.

### Toolbox

| Function / macro | What it does |
| ---------------- | ------------ |
| `walk(pt, va, alloc)` | pointer to the leaf PTE of `va`; `alloc=1` creates missing levels; 0 if a level is missing or `kalloc()` failed |
| `PTE2PA(pte)`, `PA2PTE(pa)` | PTE ↔ start of the physical page |
| `PTE_V PTE_R PTE_W PTE_X PTE_U PTE_A PTE_D` | the flag bits |
| `uvmcreate()` | an empty page table |
| `uvmunmap(pt, va, n, do_free)` | zero `n` leaf PTEs, free their pages if `do_free`; skips unmapped pages |
| `freewalk(pt)` | free the page-table pages; panics if a leaf is still mapped |
| `kalloc()`, `kfree(pa)` | one physical page |
| `memmove(dst, src, n)` | copy bytes; physical addresses work in the kernel |
| `sfence_vma()` | flush the TLB |
| `pgdump(va, n)` (user) | print the PTEs |

### Page tables (q0)

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

- **Sv39.** Three 9-bit indexes (`PX(2, va)`, `PX(1, va)`, `PX(0, va)`) and a 12-bit offset. `walk()` returns 0 when a level is missing; do not dereference it.
- **Offset.** `PTE2PA(pte)` is the start of the page; add `va % PGSIZE`. Compare `walkaddr()` (no offset) with `pa0 + (dstva - va0)` in `copyout()`.
- **`PTE_U`.** The trampoline, trapframe and stack guard page (`uvmclear()` in `exec()`) are mapped without it; an untouched lazy page has no PTE. Both give `-1`.
- **`-1`** as `uint64` is all bits set.

### Dirty bits and the TLB (q1)

- **The CPU sets** `PTE_D` on a user store and `PTE_A` on any access (`kernel/start.c`, `MENVCFG_ADUE`); it never clears them. You do: `*pte &= ~PTE_D`.
- **Flush.** The TLB caches translations. Without `sfence_vma()` the CPU may keep a cached entry with `D` set and never write the bit again, so a later store goes unnoticed.
- **`read()`.** `copyout()` writes through the kernel page table (all RAM mapped 1:1), so the user's PTE is untouched.
- **Mask.** Bit `i` is `1UL << i`; `1 << i` is undefined for `i >= 32`. `sys_pgdirty()` copies the mask out for you.

### Permissions (q2)

- **A store without `PTE_W`** traps with `scause` 15. `usertrap()` calls `vmfault()`, which returns 0 for a mapped page, so the process is killed (`setkilled()`). Loads still work.
- **Change no page on error:** one loop that checks, then one that writes.

### The snapshot (q3)

```
user page table (p->pagetable)      physical pages          shadow page table (p->shadow)
 L0[5]  V R W U A D  -----------> page 0x87F27000            L0[5]  V R  ---> copy 0x87F2C000
 L0[6]  V R . U A .  -----------> page 0x87F54000            L0[6]  V R  ---> copy 0x87F2B000
 L0[7]  V R W U A .  -----------> page 0x87F53000            L0[7]  no copy yet

checkpoint: for each page with no copy or with D:  memmove(copy, page), clear D
rollback:   for each page with D:                  memmove(page, copy), clear D
```

- **Shadow.** `walk(p->shadow, va, 1)` gives the leaf slot, creating the levels on the way.
- **A copy** is `kalloc()` + `memmove()`, installed as `*spte = PA2PTE(mem) | PTE_V | PTE_R`. Without one of `R/W/X`, `freewalk()` takes the leaf for a page-table page and panics.
- **By physical address.** `memmove((void *)PTE2PA(*spte), (void *)PTE2PA(*pte), PGSIZE)` copies a user page into its shadow; the reverse restores it, read-only or not.
- **"Has a copy"** = `walk(p->shadow, va, 0)` gives a PTE with `PTE_V`. Copy a page if it has no copy or has `PTE_D`; the first call copies all.
- **Free** = `uvmunmap(p->shadow, va, n, 1)`, then `freewalk(p->shadow)`, then reset the fields. On out of memory, call `ckpt_free()` before returning `-1`.
- **`fork()`** copies the PTEs with their flags, so a child may start dirty; harmless, the first `checkpoint` copies everything. `p->shadow` is not copied.
- **`sbrk(-n)`** can unmap part of the region after a `checkpoint`; `rollback` must check.

## Trace Code

Lab quiz scope. No report.

- Address translation (`kernel/vm.c`, `kernel/riscv.h`):
  1. `walk()`: how `PX(level, va)` splits an address, what `PTE2PA` / `PA2PTE` do, why `walk()` can return 0 or a PTE without `PTE_V`.
  2. `walkaddr()` and `copyout()`: how the kernel reaches a user page. Why does a `read()` into a user buffer **not** set `PTE_D`?
  3. `mappages()`, `uvmalloc()`, `uvmunmap()`, `freewalk()`: how a page table is built and torn down.
- Lazy allocation: `sys_sbrk()` with `SBRK_LAZY` -> `usertrap()` (scause 13 / 15) -> `vmfault()` -> `mappages()`. What happens on a store to a mapped, read-only page? Which line kills the process?
- `PTE_A` / `PTE_D`: who sets them (`kernel/start.c`, `MENVCFG_ADUE`), who clears them, why `sfence_vma()` after changing a PTE. What could go wrong with more than one CPU?
- `exec()` and `kfork()` (`uvmcopy()`): what happens to the page table, the PTE flags and the snapshot.

## Rules

- Do not change these files; they are overwritten when grading: `user/` (local tests are fine), `mkfs/`, `Makefile`, `kernel/param.h`, `kernel/syscall.h`, `grade`, `test-xv6.py`, `.devcontainer/`, `lab`, `.gitattributes`.
- Do not add kernel source files.
- One CPU; do not change that.

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

- The TAs use their own copies of `./grade` and the files listed under Rules.
- Build failure: 0 for the lab.
- Kernel panic or hang: 0 for that test or seed.
- Timeout: 15 s per test or seed (a correct run takes under a second; `ckpt_leak` a few seconds).

**Suggestions for Testing**

Passing `./grade` does not guarantee passing the hidden tests:

- Add tests to `user/vmcheck.c` and check the `ev` lines by hand (the file is overwritten when grading).
- Run `vmtest` with a few seeds (`7`, `42`) and follow a short run by hand.
- Run `usertests -q`: the rest of xv6 must still work.

Hidden tests include 64-page ranges, partly lazy regions, read-only pages inside a region, and processes that fork, exec and exit with a snapshot.

### Lab quiz (4 points)

Covers the Trace Code section, and may cover your implementation.

## Submission

1. Commit on `main`. The last commit on `main` in your zip is graded.
2. Zip the whole `os-xv6-lab2-student` folder, including `.git`, as `<student_id>.zip`. Run `make clean` first. From the folder that contains it:

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
