/******************************************************************************
 * OS/2 Screen Blanker.
 * Copyright (c) 1991 Ballard Consulting
 * Alan Ballard
 ******************************************************************************/

#define VERSION 131             /* actually n.mx (1.3b) */

#if defined(DEBUGFILE)
#define DEBUG
#endif

typedef ULONG NEAR *NPULONG;

/* Structure for the colour values used. */
typedef struct _colours {
   UCHAR           colBackground;
   UCHAR           colFrame;
   UCHAR           colText;
}               COLOURS;
typedef COLOURS FAR *PCOLOURS;

#define COLOUR(col,i) (*(&col.colBackground+i))

/* Debugging information (trace buffer) */

/* codes for various trace points */
typedef enum {
   tpNone, tpInit, tpStartPgm, tpStartSem, tpStartSg, tpStop, tpMux,
   tpPopUp, tpEndPopUp, tpKillProcess, tpMonOpen, tpMonReg,
   tpMonAction, tpWinTerm, tpMouOpen, tpMouClose, tpDosInit, tpDosCheck,
   tpWarnPopUp, tpWarnEndPopUp, tpWinSem, tpRun
}               TRACEPOINT;

typedef struct _traceentry {
   TRACEPOINT      tp;          /* trace point code */
   BOOL            bBefore;     /* TRUE -> before, FALSE -> after */
   int             rc;          /* return code if after API */
   ULONG           ulMisc;      /* anything else */
}               TRACEENTRY;

#define NTRACE (65536 / sizeof(TRACEENTRY))

#if defined(DEBUG)
#define TRACE(tp, before, rc, misc) \
      if (pSharedseg -> bDebug) \
         ScrnTrace(tp, before, rc, (void FAR *)misc);
#else
#define TRACE(tp, before, rc, misc) /* no expansion */
#endif

/* Shared memory segment containing parameters etc. */
typedef struct _sharedseg {
   int             usVersion;   /* version of program installed */
   int             iTimeout;    /* interval to blank (in seconds) */
   BOOL            bWithdraw;   /* TRUE -> go away */
   BOOL            bNoClose;    /* TRUE -> not allowed to close */
   BOOL            bShowClock;  /* TRUE -> do time display while blanked */
   BOOL            bCanDOS;     /* TRUE -> DOS driver installed */
   BOOL            bMouse;      /* TRUE -> Mouse installed. */
   BOOL            bDOS;        /* TRUE -> blank in DOS session */
   BOOL            bPM;         /* TRUE -> blank in PM session */
   BOOL            bNonPM;      /* TRUE -> blank in non-PM session */
   BOOL            bBlanked;    /* TRUE -> screen currently blanked */
   PID             pidBlanker;  /* -1  -> none yet */
   PID             pidInstaller;/* -1 -> installer done */
   PID             pidMonitor;  /* -1 -> no monitor task */
   COLOURS         colours;     /* time/date colours */
   ULONG           semStartup;  /* signal that process has got started */
   ULONG           semMain2Back;/* signal background parameters have changed */
   ULONG           semAction;   /* signal something happened (mouse or keyb) */
#if defined(CMD)
   /* Extra fields for logout program. */
   int             iWarning;    /* Time for warning/beep */
   int             iRun;        /* Time for running timeout program */
   BOOL            bRun;        /* Run the timeout command... */
   BOOL            bBeep;       /* Warning beep wanted */
   BOOL            bClose;      /* Close window after run */
#define MAXWARNLEN 80
   char            szWarning[MAXWARNLEN+1];  /* warning message */
   char            szTimeoutCmd[CCHMAXPATH+1]; /* command plus parameter */
#endif
#if defined(DEBUG)
   int             bDebug;      /* debugging parameter specified */
#define TRACEMEM   1
#define TRACEFILE  2
   SEL             selTrace;    /* selector for trace buffer */
   TRACEENTRY FAR *pNextTrace;  /* place to put next trace entry */
   TRACEENTRY FAR *pStartTrace;
   TRACEENTRY FAR *pEndTrace;   /* start and end of trace buffer */
#endif
#if defined(DEBUGFILE)
   HFILE          hfTrace;      /* handle for trace file */
#endif
}               SHAREDSEG;
typedef SHAREDSEG FAR *PSHAREDSEG;

/* TimeInfo mostly comes from the Dos CountryInfo, but includes AM/PM
 * indicators that are only available from the control panel. */
typedef struct _TimeInfo {
   USHORT          fsDateFmt;
   UCHAR           fsTimeFmt;
   CHAR            szTimeSeparator[2];
   CHAR            szAM[3];
   CHAR            szPM[3];
}               TIMEINFO;
typedef TIMEINFO NEAR *NPTIMEINFO;

/* Longest month and daynames we support.  Note the actual lengths
 * are determined by the resource file.  Currently, they are all
 * three characters. */
#define MAX_NAMELEN      10
/* The above restriction guaranteed that boxes will never get bigger than
 * following */
#define MAX_BOXLEN       (2*MAX_NAMELEN + 20)

/* Information needed for date/time display */
typedef struct _dtinfo {
   USHORT          xScreen;     /* screen width */
   USHORT          yScreen;     /* screen height */
   USHORT          xLast;       /* last box top corner */
   USHORT          yLast;
   USHORT          usLen;       /* length of time string */
   ULONG           ulLastMoveTime;

   BYTE            bBox;        /* attribute for the box part */
   BYTE            bDate;       /* attribute for the text part */
   char            achBoxTop[MAX_BOXLEN]; /* display for the box */
   char            achBoxBot[MAX_BOXLEN];
   char            achBoxBody[MAX_BOXLEN];
   char            szDayName[MAX_NAMELEN + 1];
   char            szMonthName[MAX_NAMELEN + 1];
   USHORT          LastDay;
   USHORT          LastMonth;
   USHORT          LastMinutes;  /* minutes at last update. */
   USHORT          xSeconds;    /* position of seconds display */
   TIMEINFO        timei;       /* for time/date format */

   HTIMER          htimSec;     /* one second timer */
}               DTINFO;
typedef DTINFO NEAR *NPDTINFO;

#define SHAREDSEGNAME "\\SHAREMEM\\SCRNBLNK.SEG"
#define TIMERSEMNAME  "\\SEM\\SCRNBLNK.TIM"
#define TRACESEMNAME  "\\SEM\\SCRNBLNK.TRC"
#define DLLNAME       "SCRNBLNK"      /* no extension allowed.... */
#define DRIVERNAME    "SCRNBL$$"

#define ERRORCHK(msg, rc) ErrorPopUp(msg, rc)

void FAR        ScrnBlnk(NPSZ);
void FAR        ScrnMonitor(void);
void FAR        ErrorPopUp(PSZ, int);
void FAR        StartSession(NPSZ, PSZ);
void FAR        InitBlanker(void);
void FAR        ChangeBlanker(void);
void FAR        PopupBox(USHORT idDlg);
void FAR        MessageBox(HWND hwnd, USHORT idRes, USHORT flStyle);
void FAR        CriticalError(USHORT);

/* Functions used to get and display the time/date box */
void FAR        InitClock(NPDTINFO, PCOLOURS);
void FAR        BuildClock(NPDTINFO);
void FAR        DrawClock(NPDTINFO, USHORT, USHORT, HVPS);
void FAR        ShowTime(NPDTINFO);

/* Functions for window hook in the scrnblnk.dll. */
USHORT FAR PASCAL ScrnBlnkVersion(void);
#if defined(DEBUG)
PULONG FAR PASCAL ScrnBlnkInstallHook(HAB, PQMSG FAR *);
#else
PULONG FAR PASCAL ScrnBlnkInstallHook(HAB);
#endif
BOOL   FAR PASCAL ScrnBlnkReleaseHook(void);

/* Functions used for tracing */
#if defined(DEBUG)
void            ScrnTrace(TRACEPOINT, BOOL, int, void FAR *);
#endif
