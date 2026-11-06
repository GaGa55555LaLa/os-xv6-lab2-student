# xv6 Virtual Memory Lab

Everything is on branch `main`; the spec is `vm-spec.md`. Four problems, all in `kernel/vm.c`:

| Problem | System call | Points |
| ------- | ----------- | ------ |
| q0 | `vatopa(va)`: virtual to physical address | 10 |
| q1 | `pgdirty(va, n, &mask)`: read and clear the dirty bits | 10 |
| q2 | `pgprotect(va, n, writable)`: make pages read-only or writable | 10 |
| q3 | `checkpoint(va, n)` / `rollback()`: an incremental snapshot of a memory region | 70 |

Do them in order: q3 uses what you learn in q0–q2.

## Grading

This xv6 project is worth **7 points** of the course grade: Implementation 3, Lab quiz 4.

### Implementation (3 points)

The four problems add up to 100 points, scaled to 3:

| Problem | Points | Public (40%) | Private (60%) |
| ------- | ------ | ------------ | ------------- |
| q0 `vatopa` | 10 | `./grade` (4 tests) | hidden tests |
| q1 `pgdirty` | 10 | `./grade` (4 tests) | hidden tests, 10 hidden `vmtest` seeds |
| q2 `pgprotect` | 10 | `./grade` (4 tests) | hidden tests |
| q3 `checkpoint` / `rollback` | 70 | `./grade` (10 tests) | hidden tests, 10 hidden `vmtest` seeds |

**Submission**
- The TAs grade the last commit on `main` in your zip. Uncommitted changes are not graded.
- The TAs use their own copies of `./grade` and the protected files listed under Rules in `vm-spec.md`.
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

Covers the Trace Code section of `vm-spec.md`, and may also cover your implementation.

## Getting started and submission

1. Clone this repo:

```sh
git clone https://github.com/GaGa55555LaLa/os-xv6-lab2-student.git
```

2. Follow `vm-spec.md`, and commit your work on `main`.
3. Zip the whole `os-xv6-lab2-student` folder, including the hidden `.git` folder, as `<student_id>.zip`. Run `make clean` first to keep it small. From the folder that contains `os-xv6-lab2-student`:

```sh
zip -r <student_id>.zip os-xv6-lab2-student                 # macOS / Linux
tar -a -c -f <student_id>.zip os-xv6-lab2-student           # Windows (PowerShell)
```

4. Upload `<student_id>.zip` to eLearn.

Do not push your work to a public repo (for example a fork of this one): everyone could see your code.

## Deadline

**2026/11/23 23:59 (UTC+8).** Your zip must be uploaded to eLearn by then.

## Using AI tools

- You may use generative AI tools (ChatGPT, Claude, Copilot, ...) for this lab.
- You are responsible for every line you submit. The lab quiz (4 of the 7 points) asks you to trace the code and explain your implementation.
- Do not share your code with other students.
