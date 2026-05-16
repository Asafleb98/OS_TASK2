#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

//MY_NOTE -  Task 0: Pseudo-Random Number Generator (PRNG) state and lock
static uint random_state = 1;
struct spinlock random_lock;


uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
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
  if(growproc(n) < 0)
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
  while(ticks - ticks0 < n){
    if(killed(myproc())){
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

//MY_NOTE - new function for generating the random number
void lcg_srand(uint seed) {
  acquire(&random_lock);
  random_state = seed;
  release(&random_lock);
}

uint lcg_rand(void) {
  acquire(&random_lock);
  // Parameters for the LCG: a = 1664525, b = 1013904223
  random_state = 1664525 * random_state + 1013904223;
  uint result = random_state;
  release(&random_lock);
  return result;
}

void lcg_init(void) {
  initlock(&random_lock, "random_lock");
}

//rappers for the randum functions
uint64 sys_lcg_srand(void) {
  int seed;
  argint(0, &seed);      // שולפים את הזרע (הפונקציה לא מחזירה ערך)
  lcg_srand((uint)seed); 
  return 0;
}

uint64 sys_lcg_rand(void) {
  return lcg_rand(); 
}