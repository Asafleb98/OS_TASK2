#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

// Task 0: Pseudo-Random Number Generator (LCG)
static uint random_state = 1;
static struct spinlock random_lock;

void lcg_init(void) {
  initlock(&random_lock, "random_lock");
}

// Raw kernel functions (callable from other kernel files)
void lcg_srand(uint seed) {
  acquire(&random_lock);
  random_state = seed;
  release(&random_lock);
}

uint lcg_rand(void) {
  // Parameters for the LCG: a = 1664525, b = 1013904223
  acquire(&random_lock);
  random_state = 1664525 * random_state + 1013904223;
  uint result = random_state;
  release(&random_lock);
  return result;
}

// Standard xv6 system calls
uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
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
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if (growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;
  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred since start.
uint64
sys_uptime(void)
{
  uint xticks;
  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Task 0 syscall wrappers (LCG)
uint64
sys_lcg_srand(void)
{
  int seed;
  argint(0, &seed);
  lcg_srand((uint)seed);
  return 0;
}

uint64
sys_lcg_rand(void)
{
  return lcg_rand();
}

// Task 1 syscall wrappers (GID + Israeli Lock)
uint64
sys_setgid(void)
{
  int gid;
  argint(0, &gid);
  myproc()->gid = gid;
  return 0;
}

uint64
sys_getgid(void)
{
  return myproc()->gid;
}

uint64
sys_israeli_create(void)
{
  int favoritism;
  argint(0, &favoritism);
  return israeli_create(favoritism);
}

uint64
sys_israeli_destroy(void)
{
  int lock_id;
  argint(0, &lock_id);
  return israeli_destroy(lock_id);
}

uint64
sys_israeli_acquire(void)
{
  int lock_id;
  argint(0, &lock_id);
  return israeli_acquire(lock_id);
}

uint64
sys_israeli_release(void)
{
  int lock_id;
  argint(0, &lock_id);
  return israeli_release(lock_id);
}

// Task 2 syscall wrappers 
uint64
sys_scores_nullify(void)
{
  return scores_nullify();
}

uint64
sys_inc_score(void)
{
  int team_id;
  argint(0, &team_id);
  return inc_score(team_id);
}

uint64
sys_get_score(void)
{
  int team_id;
  argint(0, &team_id);
  return get_score(team_id);
}

uint64
sys_get_leading_score(void)
{
  return get_leading_score();
}