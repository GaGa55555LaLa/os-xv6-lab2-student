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

Do them in order: q3 uses what you learn in q0–q2.

The story ends here: the sections below are the spec, and `vmcheck` and `./grade` test the system calls, not a game. `savegame` is a toy that plays with them, so you can watch your kernel work; it is not graded.

## Modifications (optional reading)

The following is already done; you do not need to implement it:

- `PTE_A` and `PTE_D` in `kernel/riscv.h`: the hardware sets `PTE_A` on any access to a page and `PTE_D` on a store, and never clears them.
- `struct proc` has `shadow`, `ckpt_va` and `ckpt_n` for the snapshot. `freeproc()` and `exec()` call `ckpt_free()`.
- The system call plumbing (`kernel/syscall.*`, `kernel/sysproc.c`, `user/user.h`, `user/usys.pl`) and `vmrange_ok()` in `kernel/vm.c`.
- `sbrklazy(n)` grows the heap without mapping pages: a page is mapped on its first access, in `vmfault()`.
- User programs: `vmcheck`, `vmtest`, `savegame`. Self-check: `./grade`.

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
- Windows: if `./grade` prints `env: $'python3\r': No such file or directory`, commit your changes first, then run:

```sh
git config core.autocrlf false
git rm -r -q --cached .
git reset -q --hard
```

- You can also open the repo with VS Code [Dev Containers](https://code.visualstudio.com/docs/devcontainers/containers).

### Build and run

```sh
make qemu                     # 1 CPU only
$ vmcheck                      # 22 small fixed tests
$ vmcheck ckpt_incr            # one of them
$ vmtest 42                    # a random workload; 42 is the seed
$ savegame                     # the toy game (not graded); q to quit
```

- Self-check: `./grade`.

### `vmcheck` output

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

`vmtest <seed>` draws a sequence of writes, `pgdirty`, `checkpoint` and `rollback` calls on a 16-page region and prints the result of each. The hidden tests use seeds you do not know; the public repo has no reference output, so check a short run against the rules by hand.

## Trace Code

This is in the scope of the lab quiz. No report is needed.

- Address translation (`kernel/vm.c`, `kernel/riscv.h`):
  1. `walk()`: how `PX(level, va)` splits a virtual address into three indexes, what `PTE2PA` / `PA2PTE` do, and why `walk()` can return 0 or a PTE without `PTE_V`.
  2. `walkaddr()` and `copyout()`: how the kernel reaches a user page. Why does a `read()` into a user buffer **not** set that page's `PTE_D`? (Which page table does the kernel use for the store?)
  3. `mappages()`, `uvmalloc()`, `uvmunmap()`, `freewalk()`: how a page table is built and torn down.
- Lazy allocation: `sys_sbrk()` with `SBRK_LAZY` -> `usertrap()` (scause 13 / 15) -> `vmfault()` -> `mappages()`. What happens on a store to a mapped page that is not writable? Which line kills the process?
- `PTE_A` / `PTE_D`: who sets them (`kernel/start.c`: `MENVCFG_ADUE`), who clears them, and why the TLB must be flushed (`sfence_vma()`) after the kernel changes a PTE. What could go wrong with more than one CPU?
- `exec()` and `kfork()` (`uvmcopy()`): what happens to the page table, the PTE flags and the snapshot of the process.

## Implementation

Fill in the places marked `TODO` in `kernel/vm.c`. The system calls take a user virtual address `va`, a page count `n`, and return `-1` on error. In every range, `va` must be page-aligned and `1 <= n <= 64`; otherwise return `-1` and change nothing.

A page is **mapped** when its PTE exists and has both `PTE_V` and `PTE_U`. A lazily allocated page that was never touched, the stack guard page, the trampoline and the trapframe are not mapped in this sense.

**Only user-mode stores count** as writes everywhere below: the hardware sets `PTE_D` when user code stores to a page. The kernel writing into a user page (for example `read()`) does not set it, and you do not need to handle that.

### q0: `uint64 vatopa(void *va)` (10 points)

*In the game: the `m` (map) command, which shows where every page really lives.*

The physical address that `va` maps to, with the page offset, or `-1` if `va` is not mapped (or `va >= MAXVA`). Do not map anything: a lazy page that was never touched gives `-1`.

### q1: `int pgdirty(void *va, int n, uint64 *mask)` (10 points)

*In the game: the `d` command, "which pages changed since the last save?"*

For the `n` pages starting at `va`: set bit `i` of `*mask` if page `i` has `PTE_D`, clear `PTE_D` on those pages, flush the TLB, and return how many bits are set. A page that is not mapped is clean. Only the range is checked; unmapped pages in it are not an error.

### q2: `int pgprotect(void *va, int n, int writable)` (10 points)

*In the game: the anti-cheat. The gold page is locked; `c` writes to it anyway and the process is killed.*

Make the `n` pages at `va` read-only (`writable == 0`: clear `PTE_W`) or writable again (set `PTE_W`), flush the TLB, return 0. If any page in the range is not mapped, return `-1` and change no page. A later store to a read-only page is a page fault that kills the process (`usertrap()` already does this).

### q3: `checkpoint` / `rollback` (70 points)

*In the game: `s` (save) and `l` (load).*

An incremental snapshot of one region of the process's memory, kept by the kernel. The design is fixed: the copies live in a second page table, `p->shadow`, whose leaf PTEs point at the kernel's copies of the pages. Keep the region in `p->ckpt_va` / `p->ckpt_n`.

`int checkpoint(void *va, int n)`

- The region must be mapped entirely; otherwise `-1`.
- The first call creates the shadow page table (`uvmcreate()`) and copies every page of the region. Return the number of pages copied (`n`).
- A later call must give the same region, otherwise `-1`. It copies only the pages that have `PTE_D` set, and returns how many it copied (0 if nothing was written).
- Every call clears `PTE_D` on the whole region and flushes the TLB.
- If a page cannot be allocated, return `-1` and leave the process without a snapshot (free what was allocated).

`int rollback(void)`

- `-1` if the process has no snapshot, or a page of the region is no longer mapped (the heap shrank into it).
- Copies back, from the shadow copies, only the pages that have `PTE_D` set, clears `PTE_D` on them, flushes the TLB, and returns how many it restored. A page that was written and then read by `pgdirty()` is clean, so `rollback` does not touch it: the dirty bit is the only record of a write.

`void ckpt_free(struct proc *p)`

- Frees the copies and the shadow page table, and resets the three fields. Already called from `freeproc()` and `exec()`. A process made by `fork()` starts without a snapshot (its `shadow` is 0; nothing to do).

## Hints

What you need to know, in the order you need it. Everything named here is in `kernel/vm.c` or `kernel/riscv.h` unless said otherwise.

### Page tables (q0)

- Sv39: a virtual address has 39 bits, three 9-bit indexes (`PX(2, va)`, `PX(1, va)`, `PX(0, va)`) and a 12-bit page offset. `walk(pagetable, va, 0)` follows the three levels and returns a pointer to the leaf PTE, or 0 when an intermediate page-table page does not exist. Never dereference that 0.
- A PTE is `PPN << 10 | flags`. `PTE2PA(pte)` drops the flags and gives the **start of the physical page**. The physical address of `va` is that plus the page offset, `va % PGSIZE` (or `va & (PGSIZE - 1)`). Compare `walkaddr()`, which returns no offset, with `pa0 + (dstva - va0)` in `copyout()`.
- Flags: `PTE_V` (valid), `PTE_R` / `PTE_W` / `PTE_X`, `PTE_U` (user mode may use the page), `PTE_A`, `PTE_D`. A page the user can access has both `PTE_V` and `PTE_U`. The trampoline, the trapframe and the stack guard page (`uvmclear()` in `exec()`) have PTEs without `PTE_U`; a lazy page that was never touched has no PTE at all.
- The return type is `uint64`, so `-1` is `(uint64)-1`, all bits set; the user library compares against that.

### Dirty bits and the TLB (q1)

- The CPU sets `PTE_D` in the PTE when a user-mode store hits the page, and `PTE_A` on any access (`kernel/start.c` enables this with `MENVCFG_ADUE`). The hardware never clears them; you do, with `*pte &= ~PTE_D`.
- The TLB (the CPU's cache of recent translations) may hold a copy of the PTE. After changing a PTE, call `sfence_vma()`, which flushes it, so the next access reads the PTE from memory again. Otherwise the CPU can keep using a cached entry that already has `D` set, never write the bit again, and a later store goes unnoticed.
- Only user-mode stores reach the user's PTE. `copyout()` writes through the kernel page table (`kvmmake()` maps all of RAM at its own physical address), so a `read()` into the region marks nothing dirty.
- Bit `i` of a 64-bit mask is `1UL << i`. `1 << i` is an `int` shift: for `i >= 32` it is undefined. Build the mask in your own variable; `sys_pgdirty()` in `kernel/sysproc.c` already copies it out to the user.

### Permissions (q2)

- Clear `PTE_W` and the next user store to that page traps with `scause` 15 (store page fault). `usertrap()` in `kernel/trap.c` calls `vmfault()`, which returns 0 because the page is already mapped, so the process is killed with `setkilled()`. Loads still work. Flush the TLB after changing the bit, or the old permission may stay cached.
- "Change no page on error" means two loops: the first only checks, the second only writes.

### The snapshot (q3)

- `uvmcreate()` returns an empty top-level page-table page. `walk(p->shadow, va, 1)` allocates the intermediate pages and returns the leaf slot for `va`; it returns 0 only when `kalloc()` failed.
- A copy is a page from `kalloc()`, filled with `memmove()`, installed as `*spte = PA2PTE(mem) | PTE_V | PTE_R`. A leaf needs `PTE_V` and at least one of `PTE_R` / `PTE_W` / `PTE_X`: `freewalk()` treats an entry with none of them as a page-table page and panics.
- The kernel can read or write any physical page by its address, because the kernel page table maps all RAM 1:1. So `memmove((void *)PTE2PA(*spte), (void *)PTE2PA(*pte), PGSIZE)` copies a user page into its shadow, and the reverse restores it, whatever the user PTE's `PTE_W` says.
- "The page has a copy" is `walk(p->shadow, va, 0)` returning a PTE with `PTE_V`. On the first call there are none, so every page is copied; later, copy a page if it has no copy or has `PTE_D`. The region is at most 64 pages, so a loop over `i` is fine.
- Freeing is two steps: `uvmunmap(p->shadow, va, n, 1)` frees the copies and zeroes their leaf PTEs (it skips pages that have no copy), then `freewalk(p->shadow)` frees the page-table pages (it panics if a leaf is still there). Reset the three fields. When memory runs out halfway, call `ckpt_free()` yourself before returning `-1`: the process must not keep half a snapshot.
- `fork()`: `uvmcopy()` copies the PTEs with their flags, so a child may start with `PTE_D` already set; harmless, because the first `checkpoint` copies everything anyway. `p->shadow` is not copied, so the child starts without a snapshot. `exec()` and `freeproc()` call `ckpt_free()` for you.
- `rollback` must check that the region is still mapped: `sbrk(-n)` after a `checkpoint` can unmap part of it.

## Rules

- Do not change (they are overwritten when grading): `user/` (you may add local tests there; they are overwritten), `mkfs/`, `Makefile`, `kernel/param.h`, `kernel/syscall.h`, and the grading and environment files (`grade`, `test-xv6.py`, `.devcontainer/`, `lab`, `.gitattributes`).
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

**Submission**
- The TAs grade the last commit on `main` in your zip. Uncommitted changes are not graded.
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

1. Commit your work on `main`. Uncommitted changes are not graded.
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
