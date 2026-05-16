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

// Task 1 - The Israeli Lock Data Structure
#define MAX_ILOCKS 15
#define MAX_QUEUE 16

struct israeli_lock {
  int active;               // 0 if free, 1 if created and in use
  int favoritism;           // Favoritism coefficient (0-100)
  int locked;               // 0 if available, 1 if currently held
  int owner_gid;            // GID of the process currently holding the lock
  int queue[MAX_QUEUE];     // Array of PIDs representing the waiting queue
  int queue_gid[MAX_QUEUE]; // <-- הוסף את השורה הזו (שומרת את הקבוצה של כל ממתין)
  int q_size;               // Number of processes currently in the queue
  uint guard;               // Atomic guard for synchronization (__sync_lock)
};

static struct israeli_lock israeli_locks[MAX_ILOCKS]; // Global array of 15 locks

//end of my NOTE


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

//Taks 1
uint64 sys_setgid(void) {
  int gid;
  argint(0, &gid); // שולפים את הערך
  myproc()->gid = gid; 
  return 0;
}

uint64 sys_getgid(void) {
  return myproc()->gid;
}

uint64 sys_israeli_create(void) {
  int favoritism;
  argint(0, &favoritism);
  
  if(favoritism < 0 || favoritism > 100)
    return -1; // החזרת שגיאה אם הערך לא חוקי

  // חיפוש מנעול פנוי במערך
  for(int i = 0; i < MAX_ILOCKS; i++) {
    if(__sync_bool_compare_and_swap(&israeli_locks[i].active, 0, 1)) {
      israeli_locks[i].favoritism = favoritism;
      israeli_locks[i].locked = 0;
      israeli_locks[i].q_size = 0;
      israeli_locks[i].guard = 0;
      return i; // מחזירים את האינדקס
    }
  }
  return -1; // לא נשארו מנעולים פנויים
}

uint64 sys_israeli_destroy(void) {
  int lock_id;
  argint(0, &lock_id);
  
  if(lock_id < 0 || lock_id >= MAX_ILOCKS)
    return -1;

  israeli_locks[lock_id].active = 0;
  return 0;
}

uint64 sys_israeli_acquire(void) {
  int lock_id;
  argint(0, &lock_id); // שולפים את הארגומנט בנפרד

  if(lock_id < 0 || lock_id >= MAX_ILOCKS)
    return -1;

  struct israeli_lock *lk = &israeli_locks[lock_id];
  if(lk->active == 0) return -1;

  struct proc *p = myproc();

  // 1. כניסה מאובטחת לתור
  while(__sync_lock_test_and_set(&lk->guard, 1) != 0);
  
  if(lk->q_size >= MAX_QUEUE) {
    __sync_lock_release(&lk->guard);
    return -1; // התור מלא
  }
  
  lk->queue[lk->q_size] = p->pid;
  lk->queue_gid[lk->q_size] = p->gid; 
  lk->q_size++;
  
  __sync_lock_release(&lk->guard);

  // 2. המתנה עד שנהיה ראשונים בתור
  while(1) {
    while(__sync_lock_test_and_set(&lk->guard, 1) != 0);

    if(lk->locked == 0 && lk->queue[0] == p->pid) {
      // תורנו!
      lk->locked = 1;
      lk->owner_gid = p->gid;

      // מסירים מהתור
      for(int i = 0; i < lk->q_size - 1; i++) {
        lk->queue[i] = lk->queue[i+1];
        lk->queue_gid[i] = lk->queue_gid[i+1];
      }
      lk->q_size--;

      __sync_lock_release(&lk->guard);
      return 0; 
    }

    __sync_lock_release(&lk->guard);
    
    yield(); // ויתור על המעבד
  }
}

uint64 sys_israeli_release(void) {
  int lock_id;
  argint(0, &lock_id); // שולפים את הארגומנט בנפרד

  if(lock_id < 0 || lock_id >= MAX_ILOCKS)
    return -1;

  struct israeli_lock *lk = &israeli_locks[lock_id];
  if(lk->active == 0) return -1;

  while(__sync_lock_test_and_set(&lk->guard, 1) != 0);

  lk->locked = 0; // משחררים את המנעול

  // לוגיקת הפרוטקציות
  if(lk->q_size > 0) {
    int friend_index = -1;

    for(int i = 0; i < lk->q_size; i++) {
      if(lk->queue_gid[i] == lk->owner_gid) {
        friend_index = i;
        break;
      }
    }

    if(friend_index != -1) {
      uint rand_val = lcg_rand() % 100; 
      if(rand_val < (uint)lk->favoritism) {
        // מזיזים את החבר לתחילת התור
        int temp_pid = lk->queue[friend_index];
        int temp_gid = lk->queue_gid[friend_index];

        for(int i = friend_index; i > 0; i--) {
          lk->queue[i] = lk->queue[i-1];
          lk->queue_gid[i] = lk->queue_gid[i-1];
        }

        lk->queue[0] = temp_pid;
        lk->queue_gid[0] = temp_gid;
      }
    }
  }

  __sync_lock_release(&lk->guard);
  return 0;
}

