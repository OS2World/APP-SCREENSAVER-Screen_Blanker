/* String resources for messages etc. */

#define IDS_CONFIG     1
#define IDS_INSTALL    5
#define IDS_INVVERS    6
#define IDS_ACTIVE     7
#define IDS_INVOPT     8
#define IDS_INVTIME    9
#define IDS_INVDLL     10
#ifdef CMD
#define IDS_INVWARNTIME 11
#define IDS_INVRUNTIME  12
#endif

/* Days are sequence starting at 16 */
#define IDS_SUNDAY     16
/* Months are sequence starting at 32 */
#define IDS_JANUARY    32

/* The main dialog boxes. */
#define IDD_HELLO      1
#define IDD_BYE        2
#define IDD_OPTIONS    3
#define IDD_COLORS     4
#define IDD_HELP       5
#define IDD_HELP2      6
#ifdef CMD
#define IDD_RUNSETUP   7
#define IDD_HELP3      8
#endif

/* Controls within the main dialog */
#define IDD_DOSSESS    256
#define IDD_NONPMSESS  257
#define IDD_PMSESS     258
#define IDD_PERM       259
#define IDD_SHOWCLOCK  260
#define IDD_TIME       261
#define IDD_REMOVE     262
#ifdef CMD
#define IDD_RUN        263
#endif

/* Controls within the color box. */
#define IDD_CLOCK      264
/* Scroll bar controls use 300 through 302 as ID's  */
#define IDD_COLBACK    300

#ifdef CMD
/* Controls within the run setup dialog. */
#define IDD_RUNWARNTIME 265
#define IDD_RUNWARNMESS 266
#define IDD_RUNBEEP     267
#define IDD_RUNTIME     268
#define IDD_RUNCMD      269
#define IDD_CLOSEWIN    270
#endif

/* The icon. */
#define IDI_BLNK       1
