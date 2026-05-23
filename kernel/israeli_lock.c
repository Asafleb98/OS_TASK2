#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

// External functions
extern uint lcg_rand(void);         // From Task 0 (in sysproc.c)
extern int get_gid_by_pid(int pid); // From proc.c helper

// ==========================================
// Task 1: The Israeli Lock Data Structure
// ==========================================
#define MAX_ILOCKS 15
#define MAX_QUEUE  16

struct israeli_lock {
  int  active;              // 0 if free, 1 if created and in use
  int  locked;              // 0 if available, 1 if currently held
  int  favoritism;          // Favoritism coefficient (0-100)
  int  owner_gid;           // GID of the process currently holding the lock

  int  queue[MAX_QUEUE];    // Circular buffer of waiting PIDs
  int  q_head;              // Index of the head of the queue
  int  q_size;              // Number of processes currently in the queue

  uint guard;               // Atomic guard for internal synchronization
};

// Global array of locks
static struct israeli_lock ilocks[MAX_ILOCKS];

// ==========================================
// Internal concurrency helpers
// ==========================================
static void acquire_internal(uint *lk) {
  while (__sync_lock_test_and_set(lk, 1) != 0)
    ;
  __sync_synchronize();
}

static void release_internal(uint *lk) {
  __sync_synchronize();
  __sync_lock_release(lk);
}

// ==========================================
// Initialization
// ==========================================
void israeli_init(void) {
  for (int i = 0; i < MAX_ILOCKS; i++) {
    ilocks[i].active = 0;
    ilocks[i].guard  = 0;
  }
}

// ==========================================
// Core kernel functions (callable from anywhere in the kernel)
// ==========================================
int israeli_create(int favoritism) {
  if (favoritism < 0 || favoritism > 100)
    return -1;

  for (int i = 0; i < MAX_ILOCKS; i++) {
    acquire_internal(&ilocks[i].guard);
    if (ilocks[i].active == 0) {
      ilocks[i].active     = 1;
      ilocks[i].locked     = 0;
      ilocks[i].favoritism = favoritism;
      ilocks[i].owner_gid  = -1;
      ilocks[i].q_head     = 0;
      ilocks[i].q_size     = 0;
      release_internal(&ilocks[i].guard);
      return i;
    }
    release_internal(&ilocks[i].guard);
  }
  return -1;
}

int israeli_destroy(int lock_id) {
  if (lock_id < 0 || lock_id >= MAX_ILOCKS)
    return -1;

  struct israeli_lock *lk = &ilocks[lock_id];

  acquire_internal(&lk->guard);

  // המנעול לא קיים / כבר נהרס
  if (lk->active == 0) {
    release_internal(&lk->guard);
    return -1;
  }

  // אם מישהו מחזיק את המנעול או מחכה בתור - לא הורסים
  if (lk->locked != 0 || lk->q_size > 0) {
    release_internal(&lk->guard);
    return -1;
  }

  // הכל פנוי - אפשר להרוס בבטחה
  lk->active = 0;
  release_internal(&lk->guard);
  return 0;
}

int israeli_acquire(int lock_id) {
  if (lock_id < 0 || lock_id >= MAX_ILOCKS)
    return -1;

  struct proc *p = myproc();
  struct israeli_lock *lk = &ilocks[lock_id];

  acquire_internal(&lk->guard);
  if (lk->active == 0) {
    release_internal(&lk->guard);
    return -1;
  }

  if (lk->q_size >= MAX_QUEUE) {
    release_internal(&lk->guard);
    return -1; // Queue full
  }

  // Add ourselves to the tail of the circular queue
  int tail = (lk->q_head + lk->q_size) % MAX_QUEUE;
  lk->queue[tail] = p->pid;
  lk->q_size++;
  release_internal(&lk->guard);

  // Yield loop - wait until we are the head and the lock is free
  while (1) {
    acquire_internal(&lk->guard);

    if (lk->active == 0) {
      release_internal(&lk->guard);
      return -1; // Lock was destroyed while we were waiting
    }

    if (lk->locked == 0 && lk->queue[lk->q_head] == p->pid) {
      lk->locked    = 1;
      lk->owner_gid = p->gid;

      // Remove ourselves from the head (O(1) with circular buffer)
      lk->q_head = (lk->q_head + 1) % MAX_QUEUE;
      lk->q_size--;

      release_internal(&lk->guard);
      return 0;
    }

    release_internal(&lk->guard);
    yield(); // Give up CPU instead of busy waiting
  }
}

int israeli_release(int lock_id) {
  if (lock_id < 0 || lock_id >= MAX_ILOCKS)
    return -1;

  struct israeli_lock *lk = &ilocks[lock_id];

  acquire_internal(&lk->guard);
  if (lk->active == 0 || lk->locked == 0) {
    release_internal(&lk->guard);
    return -1;
  }

  int G = lk->owner_gid;
  int c = lk->favoritism;

  lk->locked = 0;

  // Favoritism policy
  if (lk->q_size > 0) {
    int first_match_idx = -1;

    // Step 1: find the earliest waiter with gid == G
    for (int i = 0; i < lk->q_size; i++) {
      int idx = (lk->q_head + i) % MAX_QUEUE;
      int waiting_pid = lk->queue[idx];

      if (get_gid_by_pid(waiting_pid) == G) {
        first_match_idx = idx;
        break;
      }
    }

    int target_idx = lk->q_head; // Default to FIFO

    // Step 2: probability check
    if (first_match_idx != -1) {
      if ((lcg_rand() % 100) < (uint)c) {
        target_idx = first_match_idx;
      }
    }

    // Step 3: if favored, shift it to the front while preserving order of the rest
    if (target_idx != lk->q_head) {
      int favored_pid = lk->queue[target_idx];
      int curr = target_idx;

      while (curr != lk->q_head) {
        int prev = (curr - 1 + MAX_QUEUE) % MAX_QUEUE;
        lk->queue[curr] = lk->queue[prev];
        curr = prev;
      }
      lk->queue[lk->q_head] = favored_pid;
    }
  }

  release_internal(&lk->guard);
  return 0;
}

// ==========================================
// Task 2: Team Scores for Relay Race
// ==========================================
#define NUM_TEAMS 10

static int team_scores[NUM_TEAMS];
static int leading_score;
static struct spinlock scores_lock;

void scores_init(void) {
  initlock(&scores_lock, "scores_lock");
  for (int i = 0; i < NUM_TEAMS; i++) {
    team_scores[i] = 0;
  }
  leading_score = 0;
}

int scores_nullify(void) {
  acquire(&scores_lock);
  for (int i = 0; i < NUM_TEAMS; i++) {
    team_scores[i] = 0;
  }
  leading_score = 0;
  release(&scores_lock);
  return 0;
}

int inc_score(int team_id) {
  if (team_id < 0 || team_id >= NUM_TEAMS)
    return -1;

  acquire(&scores_lock);
  team_scores[team_id]++;
  int new_score = team_scores[team_id];
  if (new_score > leading_score) {
    leading_score = new_score;
  }
  release(&scores_lock);
  return new_score;
}

int get_score(int team_id) {
  if (team_id < 0 || team_id >= NUM_TEAMS)
    return -1;

  acquire(&scores_lock);
  int score = team_scores[team_id];
  release(&scores_lock);
  return score;
}

int get_leading_score(void) {
  acquire(&scores_lock);
  int score = leading_score;
  release(&scores_lock);
  return score;
}