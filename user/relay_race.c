#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Relay Race Tournament - tests the Israeli lock with multiple teams.
// Each runner (child process) competes for the baton (Israeli lock) and
// increments its team score until any team reaches TARGET_SCORE.
//
// Tested favoritism values: 0 (pure FIFO), 50 (moderate), 100 (max favoritism).
// Usage: relay_race [favoritism]   (default = 50)

#define TEAMS            3
#define RUNNERS_PER_TEAM 5
#define TARGET_SCORE     8

int main(int argc, char *argv[])
{
  scores_nullify();

  int favoritism = 50;
  if (argc > 1) {
    favoritism = atoi(argv[1]);
  }

  printf("Starting Relay Race Tournament with Favoritism: %d%%\n", favoritism);

  // 1. Create the Israeli lock (the shared baton)
  int lock_id = israeli_create(favoritism);
  if (lock_id < 0) {
    printf("Failed to create Israeli lock\n");
    exit(1);
  }

  lcg_srand(getpid());

  // 2. Parent acquires the lock BEFORE forking any runners.
  //    This guarantees that every runner gets a chance to enter the queue
  //    before the race actually starts, so favoritism takes effect from the
  //    very first release rather than only kicking in after the race already
  //    favored whichever runner happened to be scheduled first.
  if (israeli_acquire(lock_id) < 0) {
    printf("Parent failed to acquire lock\n");
    israeli_destroy(lock_id);
    exit(1);
  }

  // 3. Fork the runners. Each child sets its team gid and joins the queue.
  for (int team = 0; team < TEAMS; team++) {
    for (int runner = 0; runner < RUNNERS_PER_TEAM; runner++) {
      int pid = fork();

      if (pid == 0) {
        // Child (runner) code
        setgid(team);

        while (1) {
          // (a) Grab the baton (enter queue and wait)
          if (israeli_acquire(lock_id) == -1)
            break;

          // Re-check inside the critical section so we don't score
          // past the target if another team just won.
          if (get_leading_score() >= TARGET_SCORE) {
            israeli_release(lock_id);
            break;
          }

          // (b) Score a point for our team
          inc_score(team);
          int current_score = get_score(team);

          // (c) Print in the format requested by the assignment
          printf("Runner %d (Team %d) acquired the baton\n", getpid(), getgid());
          printf("Team %d score = %d\n", getgid(), current_score);

          // (d) Pass the baton to the next runner
          israeli_release(lock_id);
        }

        exit(0);
      }
    }
  }

  // 4. Give all 15 runners time to reach israeli_acquire and join the queue.
  sleep(20);

  // 5. Release the baton - the race officially starts now, with all
  //    runners already queued, so favoritism applies from the first handoff.
  israeli_release(lock_id);

  // 6. Wait for all runners to finish
  for (int i = 0; i < TEAMS * RUNNERS_PER_TEAM; i++) {
    wait(0);
  }

  // 7. Print final results
  printf("\n--- RACE FINISHED ---\n");
  for (int i = 0; i < TEAMS; i++) {
    printf("Final Score Team %d: %d\n", i, get_score(i));
  }

  israeli_destroy(lock_id);
  exit(0);
}