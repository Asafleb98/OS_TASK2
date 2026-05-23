#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define TEAMS 3
#define RUNNERS_PER_TEAM 5
#define TARGET_SCORE 8

int main(int argc, char *argv[])
{
  scores_nullify();

  // אם העברנו את ה-favoritism כארגומנט, נשתמש בו, אחרת ברירת המחדל היא 50
  int favoritism = 50;
  if (argc > 1)
  {
    favoritism = atoi(argv[1]);
  }

  printf("Starting Relay Race Tournament with Favoritism: %d%%\n", favoritism);

  // 1. יצירת המנעול הישראלי
  int lock_id = israeli_create(favoritism);
  if (lock_id < 0)
  {
    printf("Failed to create Israeli lock\n");
    exit(1);
  }

  // אתחול האקראיות
  lcg_srand(getpid());

  // 2. ההורה תופס את המנעול לפני שהילדים נוצרים.
  //    כך כל האצנים שייווצרו ינסו לעשות acquire, ייכנסו לתור ויחכו.
  //    זה מבטיח שכשהמרוץ מתחיל באמת - כל המתחרים כבר בתור,
  //    וה-favoritism עובד מהרגע הראשון.
  if (israeli_acquire(lock_id) < 0)
  {
    printf("Parent failed to acquire lock\n");
    israeli_destroy(lock_id);
    exit(1);
  }

  // 3. יצירת התהליכים (האצנים)
  for (int team = 0; team < TEAMS; team++)
  {
    for (int runner = 0; runner < RUNNERS_PER_TEAM; runner++)
    {
      int pid = fork();

      if (pid == 0)
      {               // קוד הבן (האצן)
        setgid(team); // קובע את הקבוצה שלו

        while (1)
        {
          // א. לוקחים את המקל (נכנסים לתור ומחכים)
          if (israeli_acquire(lock_id) == -1)
            break;

          // בדיקה כפולה בתוך האזור הקריטי (שלא ננצח בטעות פעמיים)
          if (get_leading_score() >= TARGET_SCORE)
          {
            israeli_release(lock_id);
            break;
          }

          // ב. מעלים את הניקוד
          inc_score(team);
          int current_score = get_score(team);

          // ג. מדפיסים הודעה לפי הפורמט שביקשו
          printf("Runner %d (Team %d) acquired the baton\n", getpid(), getgid());
          printf("Team %d score = %d\n", getgid(), current_score);

          // ד. משחררים את המקל לחבר הבא
          israeli_release(lock_id);
        }

        exit(0); // האצן סיים
      }
    }
  }

  // 4. נותנים לכל האצנים זמן להיכנס לתור
  //    (15 אצנים, צריכים זמן לרוץ ולקרוא ל-acquire)
  sleep(20);

  // 5. ההורה משחרר את המקל - והמרוץ מתחיל!
  //    מהרגע הזה כל האצנים כבר בתור, וה-favoritism עובד מההתחלה.
  israeli_release(lock_id);

  // 6. ממתינים שכל האצנים יסיימו
  for (int i = 0; i < TEAMS * RUNNERS_PER_TEAM; i++)
  {
    wait(0);
  }

  // 7. מדפיסים את התוצאות
  printf("\n--- RACE FINISHED ---\n");
  for (int i = 0; i < TEAMS; i++)
  {
    printf("Final Score Team %d: %d\n", i, get_score(i));
  }

  israeli_destroy(lock_id);
  exit(0);
}