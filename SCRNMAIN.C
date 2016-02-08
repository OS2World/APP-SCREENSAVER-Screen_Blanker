/******************************************************************************
 * OS/2 Screen Blanker.
 * This file contains the main program that installs the background process
 * and/or displays dialog box to process options. It may also become
 * either the background process or the monitoring process, depending on
 * the state when it is started.
 *
 * If the screen blanker is currently not installed, it installs it, by creating
 * the system segment, then starting a new process to run this in the
 * background.  It then displays a dialog box to allow setting options.
 * If the parameter /INSTALL is specified, this part is omitted.
 *
 * Parameter /DEBUG may be specified if compiled with DEBUG defined to
 * cause background task to keep a trace buffer. /DEBUGFILE causes a
 * trace to be written to the file /scrnblnk.log.
 *
 * Parameter /NOCLOSE prevents removing screen blanker once loaded.
 *
 * Copyright (c) 1991 Ballard Consulting
 * Alan Ballard
 *
 * Change history:
 *   November 20 1989.  Modified to use window hook DLL so it works with
 *                      OS/2 v 1.2.
 *   December    1989.  Debug option and further attempts to find problems
 *                      in 1.2 version.
 *   February    1990.  Removed call to open mouse that causes hangs with
 *                      update to 1.2.  No longer needed.
 *   May         1990.  More changes to try to find problems with 1.2.
 *                      Reworked to use separate process for monitors.
 *   November    1990.  Added support for blanking DOS box and NOCLOSE
 *                      option.
 *   December    1990.  Added special code to run program after
 *                      period of inactivity. (Conditional on CMD).
 *   January     1991.  Changed to switch from non-PM to PM before blanking
 *                      so non-PM session doesn't freeze.
 *   April       1991.  Version 1.3.  Fix minor problems.     
 *   June        1991.  Version 1.3b.  Fix problems when unable to start
 *                      monitor process; setup code to log trace to a file.
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INCL_DOSINFOSEG
#define INCL_DOSMEMMGR
#define INCL_DOSSESMGR
#define INCL_DOSPROCESS
#define INCL_DOSDATETIME
#define INCL_DOSSEMAPHORES
#define INCL_VIO
#define INCL_KBD
#define INCL_DOSERRORS
#define INCL_AVIO
#include <os2.h>
#include <pmtkt.h>

#include "scrnblnk.h"
#include "scrndlg.h"

void _cdecl     main(int argc, char *argv[]);

/* Following global variables are allocated here, and referenced in
 * all components. */
HAB             hab;            /* Our anchor block */
HMQ             hmq;            /* And message queue */
PGINFOSEG       pGInfo;         /* Global system info */
PSHAREDSEG      pSharedseg;     /* shared memory segment */
#if defined(DEBUG)
HSYSSEM         hssmTrace;      /* mutual exclusion on trace buffer */
#endif

/******************************************************************************/

/*
 * main program.
 *
 * This program is really four in one, depending on the state when it
 * is started.  This is done to avoid proliferating .exe files.
 * It may be any of the following:
 *    -- initial install of screen blanker
 *    -- change options for previously installed blanker
 *    -- background process that does the blanking
 *    -- background process that monitors screen groups
 *
 * It determines if the background process is running by checking the
 * existence of the named shared segment that is used to communicate,
 * and the pidBlanker field within it.
 * If it is run with the parameter INSTALL, then it just starts the
 * background process.
 * Otherwise it pops up a dialog box to allow setting options, and
 * then starts the background if necessary.
 */
void _cdecl
main(int argc, char *argv[])
{
   int             rc;
   SEL             selSharedseg;
   SEL             selLocal, selGlobal;
   PLINFOSEG       pLInfo;
   BOOL            bInstall, bInstallOnly;
   PIDINFO         pidi;
   char           *arg;
   char            szProgram[100];    /* my name for starting background */

   /* Get pointer to global info. */
   DosGetInfoSeg(&selGlobal, &selLocal);
   pGInfo = MAKEP(selGlobal, 0);

   DosGetPID(&pidi);

   /* First we try to allocate the shared segment, and if it already
    * exists, we do a quick check for the common case of starting
    * a monitor process.  This bypasses most of the rest of the code,
    * since it occurs at every session switch, and doesn't need a
    * PM environment established.
    */
   if (rc = DosAllocShrSeg(sizeof(SHAREDSEG), SHAREDSEGNAME, &selSharedseg)) {
      if (rc != ERROR_ALREADY_EXISTS)
         ERRORCHK("DosAllocShrSeg for " SHAREDSEGNAME, rc);
      if (rc = DosGetShrSeg(SHAREDSEGNAME, &selSharedseg))
         ERRORCHK("DosGetShrSeg for " SHAREDSEGNAME, rc);
      pSharedseg = MAKEP(selSharedseg, 0);

      /* Segment exists, so we're not initial installer. */
      bInstall = FALSE;

      /* Check for correct version to make sure subsequent tests are
       * safe.  This is repeated below, with an error message for mismatch. */
      if (pSharedseg->usVersion == VERSION) {

#if defined(DEBUG)
         if (pSharedseg->bDebug) {
            /* Make sure the trace buffer is addressable */
            if (rc = DosGetSeg(pSharedseg->selTrace))
               ERRORCHK("DosGetSeg", rc);
            /* Get handle for the mutex semaphore */
            if (rc = DosOpenSem(&hssmTrace, TRACESEMNAME))
               ERRORCHK("DosOpenSem for trace", rc);
         }
#endif

         /* If my parent pid is the blanker, we're the monitor process. */
         if (pidi.pidParent == pSharedseg->pidBlanker
               && pSharedseg->pidBlanker != -1) {
            /* Become the monitoring process. */
            pSharedseg->pidMonitor = pidi.pid;
            ScrnMonitor();
            /* Never returns ... either killed or exits. */
         }
      }
   }
   else {
      /* Segment didn't exist. This is the installer case. */
      pSharedseg = MAKEP(selSharedseg, 0);
      WtiLFillStruct(pSharedseg, sizeof(SHAREDSEG), 0);
      bInstall = TRUE;
   }

   /*
    * Make sure we aren't running from the config.sys file, which causes the
    * whole system to hang if we try to WinInitialize.
    * To do so, we just check our session number.
    */
   pLInfo = MAKEP(selLocal, 0);
   if (pLInfo->sgCurrent == 0) {
      CriticalError(IDS_CONFIG);
      DosExit(EXIT_THREAD, 1);
   }

   /* Initialize anchor block and create a message queue. */
   hab = WinInitialize(0);
   hmq = WinCreateMsgQueue(hab, 0);

   /* Now check the parameters. */
   bInstallOnly = FALSE;
   while (--argc) {
      arg = argv[argc];
      if (*arg == '/' || *arg == '-')
         ++arg;
      if (strcmpi(arg, "install") == 0)
         bInstallOnly = TRUE;
      else if (strcmpi(arg, "noclose") == 0)
         pSharedseg -> bNoClose = TRUE;
#if defined(DEBUG)
      else if (strcmpi(arg, "debug") == 0) {
         if(bInstall)
            /* Valid only when installing. */
            pSharedseg -> bDebug = TRACEMEM;
      }
#endif
#if defined(DEBUGFILE)
      else if (strcmpi(arg, "debugfile") == 0) {
         if (bInstall)
            pSharedseg->bDebug = TRACEFILE;
      }
#endif
      else
         MessageBox(HWND_DESKTOP, IDS_INVOPT, MB_OK | MB_ICONEXCLAMATION);
   }

   /* Figure out the installed state. We've already set bInstall according
      to whether the segment existed or not, and processed the case
      in which we're becoming the monitor process.
      States:
         segment doesn't exist:
            we're the installer
         segment exists, pidBlanker == -1, pidInstaller == -1
            we become the background process
         segment exists, pidBlanker == -1, pidInstaller != -1
            error -- screen installer/changer already active
         segment exists, pidBlanker != -1, pidInstaller == -1
            change options
         segment exists, pidBlanker == my parent
            become the device monitor process (above)
   */
   if (! bInstall) {
      USHORT usError;
      /* Segment exists.
       * Check that we don't have a different version running already, and
       * that we don't already have a dialog box active.
       */
      usError = 0;
      if (pSharedseg->usVersion != VERSION)
         usError = IDS_INVVERS;
      else if (ScrnBlnkVersion() != VERSION)
         usError = IDS_INVDLL;
      else if (pSharedseg->pidInstaller != -1 &&
          pidi.pidParent != pSharedseg->pidBlanker)
         usError = IDS_ACTIVE;

      if (usError != 0) {
         MessageBox(HWND_DESKTOP, usError, MB_OK | MB_ICONEXCLAMATION);
         WinTerminate(hab);
         DosExit(EXIT_PROCESS, 1);
      }
   }
   else {
      /* Segment didn't exist. This is the installer case. */
      pSharedseg->bWithdraw = !bInstallOnly; /* default CANCEL action */
      InitBlanker();
   }

   /* Generate the program name (with extension if it isn't there)
    * for starting background session and/or starting monitor
    * process.
    */
   WtiLStrCpy(szProgram, argv[0]);
   WtiAddExt(szProgram, ".EXE");

   if (!bInstall && pSharedseg->pidBlanker == -1) {
      /*
       * If shared segment exists, but there is no pID in it, we must be the
       * copy that is supposed to become the blanker.
       */

      /* Fill in my process id and mutate. */
      pSharedseg->pidBlanker = pidi.pid;
      /* Signal starter we're here. */
      DosSemClear(&pSharedseg->semStartup);

      PopupBox(IDD_HELLO);
      /* Get rid of message queue so shutdown doesn't hang */
      WinDestroyMsgQueue(hmq);

      ScrnBlnk(szProgram);

      /* Get back msg queue */
      hmq = WinCreateMsgQueue(hab, 0);
      pSharedseg->pidBlanker = -1;
      PopupBox(IDD_BYE);
   }
   else {
      /* Installing and/or setting options. */

      /* See if we should put up options box */
      if (!bInstallOnly) {
         /* Dialog box etc. */
         pSharedseg->pidInstaller = pidi.pid;
         ChangeBlanker();
      }

      /* Finally do the install. */
      pSharedseg->pidInstaller = -1;
      if (bInstall && !pSharedseg->bWithdraw) {
         DosSemSet(&pSharedseg->semStartup);

         StartSession(szProgram, NULL);

         /* Wait up to 30 seconds for background to get going */
         if (rc = DosSemWait(&pSharedseg->semStartup, 30 * 1000L))
            ERRORCHK("Background session failed to start", rc);
         TRACE(tpStartSem, FALSE, rc, NULL);
      }

   }
   TRACE(tpWinTerm, TRUE, 0, NULL);
   WinDestroyMsgQueue(hmq);
   WinTerminate(hab);
   TRACE(tpWinTerm, FALSE, 0, NULL);
   DosExit(EXIT_PROCESS, 0);
}

/*
 * Start the specified program as a separate, unrelated, background
 * session.  Used by installer to start the background session, and
 * ifdef CMD, to start the "inactivity logout program".
 *
 * Parameters are name of program, which must include extension and must
 * either be in the search path or include fully specified path, and
 * argument string to pass to it.
 */
void FAR
StartSession(NPSZ npszName, PSZ pszArgs)
{
   STARTDATA       stdata;
   USHORT          idSession;
   USHORT          pid;
   int             rc;
   char            szTemp[CCHMAXPATH+20];

   /* Initialize the startdata. */
   WtiLFillStruct(&stdata, sizeof(stdata), 0);
   stdata.Related = FALSE;
   stdata.Length = sizeof(stdata);
   stdata.FgBg = TRUE;
   stdata.PgmName = npszName;
   stdata.PgmInputs = pszArgs;
   TRACE(tpStartPgm, TRUE, 0, NULL);
   if (rc = DosStartSession(&stdata, &idSession, &pid)) {
      WtiLStrCpy(szTemp, "DosStartSession");
      WtiLStrCat(szTemp, npszName);
      ERRORCHK(szTemp, rc);
   }
   TRACE(tpStartPgm, FALSE, rc, NULL);
}
