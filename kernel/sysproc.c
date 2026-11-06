#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// --- Virtual memory lab -------------------------------------------------

uint64
sys_vatopa(void)
{
  uint64 va;

  argaddr(0, &va);
  return vatopa(myproc()->pagetable, va);
}

uint64
sys_pgdirty(void)
{
  struct proc *p = myproc();
  uint64 va, maskp, mask;
  int n, r;

  argaddr(0, &va);
  argint(1, &n);
  argaddr(2, &maskp);
  if ((r = pgdirty(p->pagetable, va, n, &mask)) < 0)
    return -1;
  if (copyout(p->pagetable, p->sz, maskp, (char *)&mask, sizeof(mask)) < 0)
    return -1;
  return r;
}

uint64
sys_pgprotect(void)
{
  uint64 va;
  int n, writable;

  argaddr(0, &va);
  argint(1, &n);
  argint(2, &writable);
  return pgprotect(myproc()->pagetable, va, n, writable);
}

uint64
sys_checkpoint(void)
{
  uint64 va;
  int n;

  argaddr(0, &va);
  argint(1, &n);
  return ckpt_take(myproc(), va, n);
}

uint64
sys_rollback(void)
{
  return ckpt_rollback(myproc());
}
