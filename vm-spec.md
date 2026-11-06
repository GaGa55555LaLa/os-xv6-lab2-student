## Introduction

You are adding a save system to xv6. A game keeps its state in a few pages of memory; *save* should remember them and *load* should put them back. Copying every page on every save is wasteful: between two saves the player usually touches one or two pages. The hardware already records which pages were written (the dirty bit in each page-table entry), so the kernel can copy only those.

The lab builds that in four steps, all in `kernel/vm.c`. The first three are the parts the last one needs:

| In the game | System call | Problem |
| ----------- | ----------- | ------- |
| where does this page really live? | `vatopa` | q0 |
| which pages changed since the last save? | `pgdirty` | q1 |
| anti-cheat: lock the gold page | `pgprotect` | q2 |
| save / load | `checkpoint` / `rollback` | q3 |

The story ends here: the sections below are the spec, and `vmcheck` and `./grade` test the system calls, not a game. `savegame` is a toy that plays with them, so you can watch your kernel work; it is not graded.

## Modifications (optional reading)

The following is already done; you do not need to implement it:

- `PTE_A` and `PTE_D` in `kernel/riscv.h`: the hardware sets `PTE_A` on any access to a page and `PTE_D` on a store, and never clears them.
- `struct proc` has `shadow`, `ckpt_va` and `ckpt_n` for the snapshot. `freeproc()` and `exec()` call `ckpt_free()`.
- The system call plumbing (`kernel/syscall.*`, `kernel/sysproc.c`, `user/user.h`, `user/usys.pl`) and `vmrange_ok()` in `kernel/vm.c`.
- `sbrklazy(n)` grows the heap without mapping pages: a page is mapped on its first access, in `vmfault()`.
- User programs: `vmcheck`, `vmtest`, `savegame`. Self-check: `./grade`.

## Instructions

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

Hints: `walk(p->shadow, va, 1)` allocates the intermediate page-table pages of the shadow; a leaf you create there needs `PTE_V` and at least one of `PTE_R`/`PTE_W`/`PTE_X`, or `freewalk()` mistakes it for a page-table page. The region is at most 64 pages, so a loop over `i` is fine.

## Rules

- Do not change (they are overwritten when grading): `user/` (you may add local tests there; they are overwritten), `mkfs/`, `Makefile`, `kernel/param.h`, `kernel/syscall.h`, and the grading and environment files (`grade`, `test-xv6.py`, `.devcontainer/`, `lab`, `.gitattributes`).
- Do not add kernel source files.
- Everything runs on one CPU; do not change that.

## Grading

This xv6 project is worth **7 points** of the course grade: Implementation 3, Lab quiz 4.

### Implementation (3 points)

The four problems add up to 100 points, scaled to 3:

| Problem | Tests | Points | Public (40%) | Private (60%) |
| ------- | ----- | ------ | ------------ | ------------- |
| q0 `vatopa` | `pa_*` | 10 | `./grade` (4 tests) | hidden tests |
| q1 `pgdirty` | `dirty_*` | 10 | `./grade` (4 tests) | hidden tests, 10 hidden seeds |
| q2 `pgprotect` | `prot_*` | 10 | `./grade` (4 tests) | hidden tests |
| q3 `checkpoint` / `rollback` | `ckpt_*` | 70 | `./grade` (10 tests) | hidden tests, 10 hidden seeds |

**Submission**
- The TAs grade the last commit on `main` in your zip. Uncommitted changes are not graded.
- The TAs use their own copies of `./grade` and the protected files listed under Rules.
- Build failure: 0 for the whole lab.
- Kernel panic or hang: 0 for the affected test or seed.

**Suggestions for Testing**

Passing `./grade` does not guarantee passing the hidden tests. To improve your confidence:

- Add your own tests to `user/vmcheck.c` (they are overwritten when grading) and check the `ev` lines against the rules by hand.
- Run `vmtest` with a few seeds and follow a short run by hand: after a `checkpoint`, the next `rollback` returns the number of distinct pages written in between.
- Run `usertests -q`: your changes to `freeproc()` / `exec()` paths must not break the rest of xv6.

Hidden tests include 64-page ranges, regions that are partly lazy, read-only pages inside a region, and processes that fork, exec and exit with a snapshot.

### Lab quiz (4 points)

Covers the Trace Code section, and may also cover your implementation.

## Using AI tools

- You may use generative AI tools (ChatGPT, Claude, Copilot, ...) for this lab.
- You are responsible for every line you submit. The lab quiz (4 of the 7 points) asks you to trace the code and explain your implementation.
- Do not share your code with other students.
