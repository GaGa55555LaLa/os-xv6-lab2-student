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

The four problems add up to 100 points, scaled to 3. Each problem is graded 40% on the public tests (`./grade`) and 60% on hidden tests; see `vm-spec.md`.

- The TAs rebuild and run the last commit on `main` in your zip. Uncommitted changes are not graded. A build failure gives 0 for the lab; a kernel panic or timeout gives 0 for that test.

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

**TBD.** Your zip must be uploaded to eLearn by then.

## Plagiarism check

- Do not show your code to anyone.
- The lab quiz may ask about your implementation, so do it yourself.
