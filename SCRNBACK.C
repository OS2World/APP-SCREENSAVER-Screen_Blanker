/******************************************************************************
 * OS/2 Screen Blanker.
 * This file contains the resident system background code segment. It is is
 * started when the blanker is installed, and monitors all sessions keyboard
 * and mouse input, runs the blanking display, etc.
 *
 * Copyright (c) 1991 Ballard Consulting
 * Alan Ballard
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#define INCL_DOSINFOSEG
#define INCL_DOSMEMMGR
#define INCL_DOSPROCESS
#define INCL_DOSSESMGR
#define INCL_DOSDATETIME
#define INCL_DOSSEMAPHORES
#define INCL_DOSFILEMGR
#define INCL_VIO
#define INCL_MOU
#define INCL_DOSERRORS
#define INCL_AVIO

#define INCL_WINSWITCHLIST
#define INCL_WINERRORS
#define INCL_SHLERRORS

#include <os2.h>
#include <os2misc.h>
#include <pmtkt.h>

#define TIMERINT        (10*1000L)  /* 10 second timer interval */

#include "scrnblnk.h"
#include "scrndlg.h"

void NEAR       StartBlanking(HSYSSEM hssm, NPDTINFO npdtinfo);
void NEAR       ContinueBlanking(NPDTINFO npdtinfo);
void NEAR       EndBlanking(NPDTINFO npdtinfo);

/* Routines to switch to PM screen group and back from non-PM sessions. */
void NEAR       SwitchToPM(void);
void NEAR       SwitchBack(void);

#if defined(CMD)
void NEAR       StartWarning(void);
void NEAR       EndWarning(void);
void NEAR       StartRun(void);
#endif

void NEAR       KillMonitorProcess(void);

/* Screen group being/to be monitored */
static SHORT    sgPrevious;     /* -1 for none */
/* Globals for mouse/dos interface handles. */
static HMOU     hmou;           /* opened mouse */
static HFILE    hfDOS;          /* DOS box interface driver */
/* Switch handle for screen group that was active when blanking began.
 * 0 means no session switch has been done. */
static HSWITCH  hsActive;
static PULONG   psemPM;         /* semaphore set by hook for PM session */

/* Anchor block is set globally by main program */
extern HAB      hab;
/* Pointer to system global information */
extern PGINFOSEG pGInfo;
/* Screen blanker shared memory segment. */
extern PSHAREDSEG pSharedseg;
#if defined(DEBUG)
/* Mutual exclusion for trace buffer. */
extern HSYSSEM  hssmTrace;
#endif

/******************************************************************************
 *
 *  Main routine; initializes and loops waiting for key strokes, mouse,
 *  or timer.  Starts/stops monitor process whenever screen group changes.
 *  Doesn't attempt to monitor either Dos session or PM session.
 *
 ******************************************************************************/
void FAR
ScrnBlnk(NPSZ npszName)
{
   int             rc;
   DEFINEMUXSEMLIST(mxsl, 4)    /* sem list */
   USHORT          usIndex;     /* index in sem list */
   HSYSSEM         hssmTime;    /* timer semaphore */
   ULONG           ulLastAction;/* time of last key or mouse action */
   HTIMER          hTimer;      /* timer handle */
   DTINFO          dtinfo;      /* date/time display info */
   USHORT          usOpenAction; /* info from file open */
   USHORT          cbRead;      /* count of bytes read */
   USHORT          usAction;    /* count of activity in DOS */
#if defined(CMD)
   BOOL            bWarningGiven;  /* warning popup is up */
   BOOL            bWarningWanted, bRunWanted;   /* timeout program stuff */
#endif
#if defined(DEBUG)
   PQMSG           pqmsgLast;       /* last PM message */
#endif

#if defined(DEBUGFILE)
   USHORT usOpenRes;
   if (pSharedseg->bDebug == TRACEFILE)
      /* Open the trace file.  Used by this and descendent processes. */
      DosOpen("c:\\scrnblnk.log", &pSharedseg->hfTrace,
         &usOpenRes, 0, FILE_NORMAL,
         FILE_TRUNCATE | FILE_CREATE,
         OPEN_ACCESS_WRITEONLY | OPEN_SHARE_DENYNONE
            | OPEN_FLAGS_WRITE_THROUGH,
         0);
#endif

   /* Initialize the system semaphore for the timer. */
   if (rc = DosCreateSem(CSEM_PUBLIC, &hssmTime, TIMERSEMNAME))
      ERRORCHK("DosCreateSem", rc);
   DosSemSet(hssmTime);

   /* Get handle for the DOS box. */
   if (pSharedseg -> bCanDOS) {
      if (rc = DosOpen(DRIVERNAME,     /* device SCRNBL$$        */
            &hfDOS,                    /* address of file handle */
            &usOpenAction,             /* action taken           */
            0L,                        /* size of new file       */
            FILE_NORMAL,               /* file attribute         */
            FILE_OPEN,                 /* open only */
            OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE, /* open mode */
            0L))
         ERRORCHK("DosOpen for SCRNBL$$", rc);
   }

   /*
    * Start a thread that uses a Windows Hook to note all keyboard and mouse
    * actions in the PM session.
    */
#if defined(DEBUG)
   if ((psemPM = ScrnBlnkInstallHook(hab, &pqmsgLast)) == NULL)
#else
   if ((psemPM = ScrnBlnkInstallHook(hab)) == NULL)
#endif
      ERRORCHK("Can't install PM hook", 1);
   *psemPM = 0;

   pSharedseg->semAction = 0;

   /* Build the mux sem list */
   mxsl.cmxs = 4;
   mxsl.amxs[0].zero = 0;
   mxsl.amxs[0].hsem = hssmTime;
   mxsl.amxs[1].zero = 0;
   mxsl.amxs[1].hsem = &pSharedseg->semAction;
   mxsl.amxs[2].zero = 0;
   mxsl.amxs[2].hsem = &pSharedseg->semMain2Back;
   mxsl.amxs[3].zero = 0;
   mxsl.amxs[3].hsem = psemPM;

   /* Start a timer. */
   if (rc = DosTimerStart(TIMERINT, hssmTime, &hTimer))
      ERRORCHK("DosTimerStart", rc);

   ulLastAction = pGInfo->time;
   pSharedseg->bBlanked = FALSE;
   TRACE(tpInit, FALSE, 0, NULL);

#if defined(CMD)
   bWarningGiven = FALSE;
   bRunWanted = pSharedseg->bRun && pSharedseg->szTimeoutCmd[0];
   bWarningWanted = bRunWanted && (pSharedseg->bBeep
                                 || pSharedseg->szWarning[0]);
#endif

   while (!pSharedseg->bWithdraw) {
      sgPrevious = pGInfo->sgCurrent;

      /*
       * Note we do not monitor the DOS session if it's the current one,
       * since registering against the DOS session doesn't work.
       * If we are blanking on the DOS session, we poll from
       * the timer to see if the screen blanker driver has reported any
       * activity.
       * We also don't monitor the PM session (since it won't work).
       * The windows hook monitors that session.  We skip over the
       * code to open/reopen the monitor for DOS or PM.
       */
      TRACE(tpStartSg, TRUE, 0, &sgPrevious);
      if (sgPrevious != SESS_DOS && sgPrevious != SESS_PM) {
         char            chFailName[80];
         char            chTemp[100];
         RESULTCODES     rescResults;

         /* Set the startup semaphore before starting the process. */
         DosSemSet(&pSharedseg->semStartup);
         TRACE(tpStartPgm, TRUE, 0, NULL);
         if (rc = DosExecPgm(chFailName, sizeof(chFailName), EXEC_ASYNC,
                       NULL, NULL, &rescResults, npszName)) {
            WtiLStrCpy(chTemp, "Can't start process: ");
            WtiLStrCat(chTemp, chFailName);
            ERRORCHK(chTemp, rc);
         }
         TRACE(tpStartPgm, FALSE, rc, NULL);

         /* Wait up to 30 seconds for background to get going */
         if (rc = DosSemWait(&pSharedseg->semStartup, 30 * 1000L))
            ERRORCHK("Background process failed to start", rc);
         TRACE(tpStartSem, FALSE, rc, NULL);
      }

      while ((int) pGInfo->sgCurrent == sgPrevious) {

         /*
          * Wait for timer or signals from monitor threads, window hook, or
          * another scrnblnk invocation.
          */
         TRACE(tpMux, TRUE, 0, NULL);
         if (rc = DosMuxSemWait(&usIndex, &mxsl, SEM_INDEFINITE_WAIT))
            ERRORCHK("DosMuxSemWait", rc);
         TRACE(tpMux, FALSE, rc, &usIndex);
         /* Reset the semaphore that went off. */
         DosSemSet(mxsl.amxs[usIndex].hsem);

         if (pSharedseg->bWithdraw)
            /* Leave, eventually exit the whole thing... */
            break;

         if (usIndex == 0) {
            /******** Timer event *******/
            if (pSharedseg->bBlanked) {
               /* Screen is currently blanked. This is where we
                * move the date display. */
               if (pSharedseg->bShowClock)
                  ContinueBlanking(&dtinfo);
            }
            else {
               /* If we're in DOS session, and are supposed to be monitoring
                * it, check if there has been any activity.  Otherwise,
                * just assume we've seen something and
                * continue waiting. */
               if (sgPrevious == SESS_DOS) {
                  if (pSharedseg -> bCanDOS) {
                     if ((rc = DosRead(hfDOS, &usAction, sizeof(usAction),
                           &cbRead)) != 0 || cbRead != sizeof(usAction))
                        ERRORCHK("DosRead for SCRNBL$$", rc);
                     TRACE(tpDosCheck, FALSE, rc, &usAction);
                     if (usAction != 0)
                        /* some activity, so change time stamp */
                        ulLastAction = pGInfo->time;
                  }
                  else
                     /* Just pretend something happened */
                     ulLastAction = pGInfo->time;
               }

               /* See if time to blank, and if we're in a session
                * that we are supplosed to be blanking. */
               if (pGInfo->time > ulLastAction + pSharedseg->iTimeout
                     && ((sgPrevious == SESS_PM && pSharedseg->bPM)
                        || (sgPrevious == SESS_DOS && pSharedseg->bDOS)
                        || (sgPrevious != SESS_PM && sgPrevious != SESS_DOS
                                                  && pSharedseg->bNonPM))) {
                  SwitchToPM();
#if defined(CMD)
                  /* Can't blank if warning message is up. */
                  if (!bWarningGiven)
#endif
                     StartBlanking(hssmTime, &dtinfo);
                  /* if fails,  try again next timer tick */
               }
            }

#if defined(CMD)
            /* See if we want special warning or timeout command
             * processing. */
            if (bWarningWanted &&
                  pGInfo->time > ulLastAction + pSharedseg->iWarning &&
                  !bWarningGiven) {
               if (pSharedseg->bBlanked)
                  /* Have to turn off blanker so warning can get popup */
                  EndBlanking(&dtinfo);
               SwitchToPM();
               bWarningGiven = StartWarning();
            }
            else if (bRunWanted &&
                        pGInfo->time > ulLastAction + pSharedseg->iRun) {
               if (bWarningGiven) {
                  EndWarning();
                  bWarningGiven = FALSE;
               }
               if (pSharedseg->bBlanked)
                  /* Have to turn off blanker because StartRun fails if
                   * a popup is in effect. (OS/2 bug???) */
                  EndBlanking(&dtinfo);
               StartRun();
               bRunWanted = FALSE;
               bWarningWanted = FALSE;
            }
#endif

         }  /* end of usIndex == 0 */

         else {
#if defined(DEBUG)
            if (usIndex == 3)
               TRACE(tpWinSem, TRUE, pqmsgLast->msg, &pqmsgLast->time);
#endif
            /* Key or mouse move (or options changed) */
            ulLastAction = pGInfo->time;
            if (pSharedseg->bBlanked) {
               /* Activity -- time to stop blanking. */
               EndBlanking(&dtinfo);
               if (hsActive != 0)
                  SwitchBack();
            }
#if defined(CMD)
            if (bWarningGiven) {
               EndWarning();
               bWarningGiven = FALSE;
               if (hsActive != 0)
                  SwitchBack();
            }
            /* Reset the run/warning switches in case status changed. */
            bRunWanted = pSharedseg->bRun && pSharedseg->szTimeoutCmd[0];
            bWarningWanted = bRunWanted && (pSharedseg->bBeep ||
                                            pSharedseg->szWarning[0]);
#endif
         }
      }

      /*
       * Change of screen group.  Have to kill the monitor process and start
       * one for the new screen group.  Except, if the previous session is
       * DOS session or PM session, we haven't started one.
       */
      if (sgPrevious != SESS_DOS && sgPrevious != SESS_PM)
         KillMonitorProcess();

   }
   /* Shutting down. */
   TRACE(tpStop, TRUE, 0, NULL);
   if (!ScrnBlnkReleaseHook())
      ERRORCHK("Unable to release hook", 0);
   DosTimerStop(hTimer);
   DosCloseSem(hssmTime);
   TRACE(tpStop, FALSE, 0, NULL);

}

/******************************************************************************
 * StartBlanking is called after the time interval expires, to
 * put up the blanker screen if possible.
 * If date display is requested, it initializes for it also.
 *****************************************************************************/
void NEAR
StartBlanking(
              HSYSSEM hssm,     /* system semaphore for timer */
              NPDTINFO npdtinfo)/* returned date/time information */
{
   int             rc;
   /* Constant used to hide the cursor. */
   static VIOCURSORINFO vioci = {0, 0, 0, 0xFFFF};
   USHORT          fWait = VP_NOWAIT | VP_OPAQUE;  /* param for popup */

   TRACE(tpPopUp, TRUE, 0, NULL);
   rc = VioPopUp(&fWait, 0);
   TRACE(tpPopUp, FALSE, rc, NULL);
   if (rc)
      return;

   /* Looks like we again have a version of OS/2 in which the mouse
    * doesn't work to end the popup.  Opening the device seems to fix this.
    */
   hmou = (HMOU) 0;
   if (pSharedseg -> bMouse) {
      TRACE(tpMouOpen, TRUE, 0, NULL);
      if (rc = MouOpen(0L, &hmou))
         ERRORCHK("MouOpen", rc);
      TRACE(tpMouOpen, FALSE, rc, NULL);
   }

   /* Following maybe no longer necessary with OS/2 1.2. */
   VioSetCurType(&vioci, 0);

   npdtinfo->htimSec = 0;       /* init timer handle to none */

   /* Initialize the time/date display if it is wanted. */
   if (pSharedseg->bShowClock) {
      static VIOINTENSITY vioint = {sizeof(VIOINTENSITY), 0x0002, 0x0001};
      VIOMODEINFO     viomi;

      /* Get the screen size from the mode info */
      viomi.cb = sizeof(viomi);
      VioGetMode(&viomi, 0);
      npdtinfo->xScreen = viomi.col;
      npdtinfo->yScreen = viomi.row;

      /*
       * Set the screen state to allow intense background colours instead of
       * blinking foreground.
       */
      VioSetState(&vioint, 0);

      InitClock(npdtinfo, &pSharedseg->colours);

      /*
       * Start a 1 second timer for updating the seconds. (If it fails,
       * continue without it).
       */
      DosTimerStart(1000L, hssm, &npdtinfo->htimSec);

      /* Do the initial display. */
      ShowTime(npdtinfo);

   }
   pSharedseg->bBlanked = TRUE;
}

/*
 * ContinueBlanking is called for each timer tick while the screen is
 * blanked, to update the date/time display.  Every 10 seconds,
 * it moves it.
 */
void NEAR
ContinueBlanking(NPDTINFO npdtinfo)
{
   ShowTime(npdtinfo);
}

/*
 * EndBlanking is called to restore the screen.
 */
void NEAR
EndBlanking(NPDTINFO npdtinfo)
{
   int             rc;

   pSharedseg->bBlanked = FALSE;

   if (npdtinfo->htimSec != 0) {
      rc = DosTimerStop(npdtinfo->htimSec);
   }
   if (hmou != (HMOU)0) {
      TRACE(tpMouClose, TRUE, 0, NULL);
      rc = MouClose(hmou);
      TRACE(tpMouClose, FALSE, rc, NULL);
   }

   TRACE(tpEndPopUp, TRUE, 0, NULL);
   rc = VioEndPopUp(0);
   TRACE(tpEndPopUp, FALSE, rc, NULL);
}

/******************************************************************************
 * SwitchToPM is called when about to start blanking.  It
 * switches the active session to the PM session (actually, the one in which
 * this process is running), which has the effect of allowing the OS/2
 * session to continue running in background.  Otherwise it gets suspended
 * the first time it tries to write to the screen while the VioPopUp has
 * control of the screen. (An undocumented "feature" of VioPopUp).
#if defined(CMD)
 * For special CMD version, this is called before the warning pop-up also.
#endif
 *****************************************************************************/
void NEAR
SwitchToPM()
{
   USHORT winrc;
   /* If the PM session is active, then no need to switch. Also, no point
    * in doing this for DOS, since it gets suspended by the switch
    * anyway.  Also, switching back to DOS session doesn't seem to work
    * properly. Finally, skip tis if we've already switched and not yet
    * switched back. */
   if (pGInfo->sgCurrent == SESS_PM ||
         pGInfo->sgCurrent == SESS_DOS ||
         hsActive != 0)
      return;

   /* Save the switch handle for the current process. */
   hsActive = WinQuerySwitchHandle(0, pGInfo->pidForeground);
   if (hsActive != 0) {
      winrc = WinSwitchToProgram(WinQuerySwitchHandle(0,
                                                  pSharedseg->pidBlanker));
      if (winrc != 0)
         ERRORCHK("WinSwitchToProgram for PM", winrc);
   }

   /* When we do the session switch, we may get a spurious message,
    * which undoes the blanking.  This appears to be due to a pending
    * semaphore.  Resetting the semaphore fixes this. (Though it isn't
    * supposed to, since according to documentation, the DosMuxSemWait
    * should complete anyway...)
    */
   DosSemSet(psemPM);
}

/*****************************************************************************
 * SwitchBack returns to the previous active screen group.
 *****************************************************************************/
void NEAR
SwitchBack()
{
   USHORT winrc;
   winrc = WinSwitchToProgram(hsActive);
   /* It is possible to get an error return if the switched out process
    * terminated while we were blanked.  We should really check that the 
    * process exists, but we'll just check for and ignore the error 
    * code... */   
   if (winrc != 0 && winrc != PMERR_INVALID_SWITCH_HANDLE)
      ERRORCHK("WinSwitchToProgram to switch back", winrc);
   hsActive = 0;
}

#if defined(CMD)
/******************************************************************************
 * StartWarning is called after the warning time interval expires, to
 * put up the warning message if possible.
 *****************************************************************************/
BOOL NEAR
StartWarning()
{
   int             rc;
   VIOMODEINFO     viomi;
   USHORT          usRow, usColumn, usLen;
   USHORT          fWait = VP_NOWAIT | VP_OPAQUE;  /* param for popup */
   BYTE            bAttr;

   if (pSharedseg->szWarning[0]) {
      TRACE(tpWarnPopUp, TRUE, 0, NULL);
      rc = VioPopUp(&fWait, 0);
      TRACE(tpWarnPopUp, FALSE, rc, NULL);
      if (rc)
         return FALSE;

      /* Get the screen size from the mode info */
      viomi.cb = sizeof(viomi);
      VioGetMode(&viomi, 0);

      usLen = WtiLStrLen(pSharedseg->szWarning);
      usRow = viomi.row/2;
      if (usLen > viomi.col)
         usColumn = 0;
      else
         usColumn = (viomi.col - usLen) / 2;

      bAttr = (BYTE) 16 * pSharedseg->colours.colBackground +
                   pSharedseg->colours.colText;
         if (rc = VioWrtCharStrAtt(pSharedseg->szWarning, usLen,
               usRow, usColumn, &bAttr, 0))
            ERRORCHK("VioWrtCharStrAtt", rc);
   }

   if (pSharedseg->bBeep)
      DosBeep(850,750);

   return TRUE;
}

/*
 * EndWarning is called to restore the screen.
 */
void NEAR
EndWarning()
{
   int             rc;
   TRACE(tpWarnEndPopUp, TRUE, 0, NULL);
   rc = VioEndPopUp(0);
   TRACE(tpWarnEndPopUp, FALSE, rc, NULL);
}

/*
 * Start Run runs the timeout command
 */
void NEAR
StartRun()
{
   char            chTemp[CCHMAXPATH+4];

   /* We start the program using cmd.exe, so the specified command
    * is the parameter string.... */
   if (pSharedseg -> bClose)
      WtiLStrCpy(chTemp, "/C ");
   else
      WtiLStrCpy(chTemp, "/K ");
   WtiLStrCat(chTemp, pSharedseg->szTimeoutCmd);

   TRACE(tpRun, TRUE, 0, NULL);
   StartSession("cmd.exe", chTemp);
}
#endif

/*
 * KillMonitorProcess kills the process that is monitoring keyboard
 * and mouse activity.
 */
void NEAR
KillMonitorProcess(void)
{
   int             rc;
   TRACE(tpKillProcess, TRUE, 0, NULL);
   rc = DosKillProcess(DKP_PROCESS, pSharedseg->pidMonitor);
   /* May get invalid procid error here if the monitor went away
    * of its own accord (e.g., because session switch occurred before it
    * got going.) */
   if (rc != 0 && rc != ERROR_INVALID_PROCID)
      ERRORCHK("DosKillProcess", rc);
   TRACE(tpKillProcess, FALSE, rc, NULL);
}

/*
 * Debugging -- routine to keep a circular trace buffer in shared
 * memory, and optionally to log to a trace file.
 */
#if defined(DEBUG)
void
ScrnTrace(TRACEPOINT tp,
          BOOL bBefore,
          int rc,
          void FAR * pMisc)
{
#if defined(DEBUGFILE)
   char   ac[100];
   USHORT len, rlen;
#endif
   TRACEENTRY FAR *pTrace;
   int             rcReq;

   /* Wait only 2 seconds here... if longer, something has gone wrong. */
   rcReq = DosSemRequest(hssmTrace, 2 * 1000L);

#if defined(DEBUGFILE)
   if (pSharedseg->bDebug == TRACEFILE) {
      /* Tracing to file... */
      static char    *tpName[] = {"None", "Init", "StartPgm", "StartSem",
         "StartSG", "Stop",
         "Mux", "PopUp", "EndPopUp", "KillProcess", "MonOpen", "MonReg",
         "MonAction", "WinTerm", "MouOpen", "MouClose", "DosInit",
         "DosCheck", "WarnPopUp", "WarnEndPopUp", "WinSem", "Run"};
      PIDINFO pidi;

      if (pSharedseg->hfTrace != 0) {
         /* See if the mutex semaphore had a problem. */
         if (rcReq != 0) {
            len = sprintf(ac, "\ntrace mutex rc=%d\n", rcReq);
            DosWrite(pSharedseg->hfTrace, ac, len, &rlen);
         }

         DosGetPID(&pidi);
         len = sprintf(ac, "pid %3u %s %.20s ", pidi.pid,
            bBefore ? "bef" : "aft", tpName[tp]);
         if (tp == tpMonAction)
            len += sprintf(ac+len, "datalen=%d ", rc);
         else
            len += sprintf(ac+len, "rc=%d ", rc);

         switch (tp) {
         case tpStartSg:
            len += sprintf(ac+len, "screen group=%d", *(SHORT FAR *) pMisc);
            break;

         case tpMonOpen:
         case tpMonReg:
         case tpMonAction:
            len += sprintf(ac+len, "%.4Fs", (char FAR *) pMisc);
            break;

         case tpMux:
            if (!bBefore)
               len += sprintf(ac+len, "index = %d", *(USHORT FAR *) pMisc);
            break;

         case tpWinSem:
            len+= sprintf(ac+len, "time = %ld", *(ULONG FAR *) pMisc);
            break;

         }
         len += sprintf(ac+len, "\n");
         DosWrite(pSharedseg->hfTrace, ac, len, &rlen);
      }
   }
#endif

   pTrace = pSharedseg->pNextTrace;
   pTrace->tp = tp;
   pTrace->bBefore = bBefore;
   pTrace->rc = rc;
   if (pMisc != NULL)
      pTrace->ulMisc = *(ULONG FAR *) pMisc;
   else
      pTrace->ulMisc = 0;
   if (pTrace == pSharedseg->pEndTrace)
      pTrace = pSharedseg->pStartTrace;
   else
      ++pTrace;
   pSharedseg->pNextTrace = pTrace;

   DosSemClear(hssmTrace);
}

#endif
