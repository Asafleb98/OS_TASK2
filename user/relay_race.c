#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define TEAMS 3
#define RUNNERS_PER_TEAM 5
#define TARGET_SCORE 30

int main(int argc, char *argv[]) {
  // אם העברנו את ה-favoritism כארגומנט, נשתמש בו, אחרת ברירת המחדל היא 50
  int favoritism = 50; 
  if (argc > 1) {
    favoritism = atoi(argv[1]);
  }

  printf("Starting Relay Race Tournament with Favoritism: %d%%\n", favoritism);

  // 1. יצירת המנעול הישראלי
  int lock_id = israeli_create(favoritism);
  if (lock_id < 0) {
    printf("Failed to create Israeli lock\n");
    exit(1);
  }

  // אתחול האקראיות (בשביל שלכל הרצה תהיה חלוקה קצת שונה, למרות שזה לא חובה פה)
  lcg_srand(getpid());

  // 2. יצירת התהליכים (האצנים)
  for (int team = 0; team < TEAMS; team++) {
    for (int runner = 0; runner < RUNNERS_PER_TEAM; runner++) {
      int pid = fork();
      
      if (pid == 0) { // קוד הבן (האצן)
        setgid(team); // קובע את הקבוצה שלו
        
        while (1) {
          // בודקים אם המשחק נגמר לפני שמנסים לקחת את המנעול
          if (get_score(team) >= TARGET_SCORE) {
             break;
          }

          // א. לוקחים את המקל
          israeli_acquire(lock_id);
          
          // בדיקה כפולה בתוך האזור הקריטי (שלא ננצח בטעות פעמיים)
          int current_score = get_score(team);
          if (current_score >= TARGET_SCORE) {
             israeli_release(lock_id);
             break;
          }

          // ב. מעלים את הניקוד
          inc_score(team);
          current_score = get_score(team); // קוראים שוב כדי להדפיס

          // ג. מדפיסים הודעה לפי הפורמט שביקשו
          printf("Runner %d (Team %d) acquired the baton\n", getpid(), getgid());
          printf("Team %d score = %d\n", getgid(), current_score);

          // ד. משחררים את המקל לחבר הבא
          israeli_release(lock_id);

          // ה. הולכים לישון קצת (כמו שביקשו במטלה)
          sleep(5);
        }
        
        exit(0); // האצן סיים
      }
    }
  }

  // קוד האב: ממתין לכל האצנים שיסיימו
  for (int i = 0; i < TEAMS * RUNNERS_PER_TEAM; i++) {
    wait(0);
  }

  // מדפיסים הודעת סיום ואת המנצח
  printf("\n--- RACE FINISHED ---\n");
  for (int i = 0; i < TEAMS; i++) {
     printf("Final Score Team %d: %d\n", i, get_score(i));
  }

  israeli_destroy(lock_id);
  exit(0);
}