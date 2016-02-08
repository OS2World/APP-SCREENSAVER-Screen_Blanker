/******************************************************************************
 * OS/2 Screen Blanker.
 * This file contains the routines to install and/or set options, as well
 * as error message popups etc.
 * This code is generally not required once the program is up and
 * running.
 *
 * Copyright (c) 1991 Ballard Consulting
 * Alan Ballard
 *
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INCL_DOSINFOSEG
#define INCL_DOSMEMMGR
#define INCL_DOSFILEMGR
#define INCL_DOSPROCESS
#define INCL_DOSDATETIME
#define INCL_DOSSEMAPHORES
#define INCL_VIO
#define INCL_KBD
#define INCL_DOSERRORS
#define INCL_WINDIALOGS
#define INCL_WINBUTTONS
#define INCL_WINENTRYFIELDS
#define INCL_WINSCROLLBARS
#define INCL_WINFRAMEMGR
#define INCL_WINSHELLDATA
#define INCL_WINWINDOWMGR
#define INCL_WINTIMER
#define INCL_WININPUT
#define INCL_WINSYS
#define INCL_AVIO
#include <os2.h>
#include <pmtkt.h>

#include "scrnblnk.h"
#include "scrndlg.h"
#define TIMERID    1

/* Profile data stored in the os2.ini file. */
typedef struct _scrnblnkdata {
   USHORT          usVersion;   /* To identify changes. */
   int             iTimeout;    /* interval to blank (in seconds) */
   BOOL            bShowClock;  /* TRUE -> do time display while blanked */
   BOOL            bDOS;        /* TRUE -> blank DOS session */
   BOOL            bPM;         /* TRUE -> blank PM session */
   BOOL            bNonPM;      /* TRUE -> blank non-PM sessions */
   COLOURS         colours;     /* time/date colours */
#ifdef CMD
   /* Extra fields for cmd version.  Note message and command are separate
    * profile items. */
   int             iWarning;    /* Time for warning/beep */
   int             iRun;        /* Time for running timeout program */
   BOOL            bRun;        /* Timeout command enabled */
   BOOL            bBeep;       /* Warning beep wanted */
   BOOL            bClose;      /* Closw window after run */
#endif
}               SCRNBLNKDATA;

/* Default colours (VIO codes)*/
#define COL_BACKGR 7
#define COL_FRAME  9
#define COL_TEXT   9
/* If a monochrome display is attached, we can only show black, white,
 * and intense white, because the VioPopup routine seems to insist on setting
 * mode mono, and won't allow changing it.  So the dialog box for displaying
 * colours uses the following table to map the requested colours to what
 * will actually show when the pop-up occurs.  This is needed because we
 * can't set mode mono in AVIO operation. */
static UCHAR    ColourMap[] = {0, 7, 7, 7, 7, 7, 7, 7, 0, 15, 15, 15,
      15, 15, 15, 15};

MRESULT CALLBACK PopupDlgProc(HWND, USHORT, MPARAM, MPARAM);

MRESULT CALLBACK OptionsDlgProc(HWND, USHORT, MPARAM, MPARAM);
MRESULT CALLBACK ColorsDlgProc(HWND, USHORT, MPARAM, MPARAM);
MRESULT CALLBACK ShowClockWndProc(HWND, USHORT, MPARAM, MPARAM);
void NEAR        CentreWindow(HWND);

#ifdef CMD
MRESULT CALLBACK RunSetupDlgProc(HWND, USHORT, MPARAM, MPARAM);
#endif

/* Anchor block is set globally by main program */
extern HAB      hab;
/* Pointer to system global information */
extern PGINFOSEG pGInfo;
/* Screen blanker shared memory segment. */
extern PSHAREDSEG pSharedseg;
#if defined(DEBUG)
extern HSYSSEM  hssmTrace;      /* mutual exclusion on trace buffer */
#endif

/*
 * Function to initialize the shared segment for the screen blanker.
 */
void FAR
InitBlanker()
{
   SCRNBLNKDATA    initdata;
   USHORT          cbinit;
   USHORT          usOpenAction;   /* info from file open */
   HFILE           hfDOS;          /* DOS box interface driver */

   /* Get the preferences from the profile file, or use defaults... */
   cbinit = sizeof(initdata);
   if (WinQueryProfileData(hab, "SCRNBLNK", "OPTIONS", &initdata, &cbinit)
       && cbinit == sizeof(initdata)
       && initdata.usVersion == VERSION) {
      /* Got it. */
      pSharedseg->iTimeout = initdata.iTimeout;
      pSharedseg->bShowClock = initdata.bShowClock;
      pSharedseg->bPM = initdata.bPM;
      pSharedseg->bNonPM = initdata.bNonPM;
      pSharedseg->bDOS = initdata.bDOS;
      pSharedseg->colours = initdata.colours;
#if defined(CMD)
      pSharedseg->iWarning = initdata.iWarning;
      pSharedseg->iRun = initdata.iRun;
      pSharedseg->bRun = initdata.bRun;
      pSharedseg->bBeep = initdata.bBeep;
      pSharedseg->bClose = initdata.bClose;
      /* Get the command and message strings also. */
      WinQueryProfileString(hab, "SCRNBLNK", "WARNING", "",
         pSharedseg->szWarning, MAXWARNLEN);
      WinQueryProfileString(hab, "SCRNBLNK", "TIMEOUTCMD", "",
         pSharedseg->szTimeoutCmd, CCHMAXPATH);
#endif
   }
   else {
      /* None, or version changed.  Use defaults */
      pSharedseg->iTimeout = 5 * 60;   /* 5 minutes */
      pSharedseg->bShowClock = TRUE;
      pSharedseg->bPM = TRUE;
      pSharedseg->bNonPM = TRUE;
      pSharedseg->bDOS = TRUE;
      pSharedseg->colours.colBackground = COL_BACKGR;
      pSharedseg->colours.colFrame = COL_FRAME;
      pSharedseg->colours.colText = COL_TEXT;
#if defined(CMD)
      pSharedseg->iWarning = 25*60;   /* 25 minutes */
      pSharedseg->iRun = 30*60;       /* 30 minutes */
      pSharedseg->bClose = TRUE;
#endif
   }
   pSharedseg->pidBlanker = -1;
   pSharedseg->usVersion = VERSION;

   /* See if we have a mouse */
   pSharedseg->bMouse = (BOOL) WinQuerySysValue(hab, SV_MOUSEPRESENT);

   /* See if we can interface with the DOS box by trying to open the
    * driver.  */
   if (DosOpen(DRIVERNAME,             /* device SCRNBL$$        */
         &hfDOS,                       /* address of file handle */
         &usOpenAction,                /* action taken           */
         0L,                           /* size of new file       */
         FILE_NORMAL,                  /* file attribute         */
         FILE_OPEN,                    /* open only */
         OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE, /* open mode */
         0L) == 0) {
      pSharedseg -> bCanDOS = TRUE;
      DosClose(hfDOS);
   }
   else
      pSharedseg -> bCanDOS = FALSE;

   DosSemSet(&pSharedseg->semMain2Back);

   /* If debugging, allocate trace buffer and initialize it. */
#if defined(DEBUG)
   {
      SEL             sel;
      TRACEENTRY FAR *pT;
      int             rc;
      if (pSharedseg->bDebug)
         if (DosAllocSeg((USHORT) (NTRACE * sizeof(TRACEENTRY)),
             &sel, SEG_GETTABLE))
            pSharedseg->bDebug = FALSE;   /* shouldn't happen */
         else {
            pSharedseg->selTrace = sel;
            pSharedseg->pNextTrace = pSharedseg->pStartTrace =
               MAKEP(sel, 0);
            pSharedseg->pEndTrace =
               pSharedseg->pStartTrace + (NTRACE - 1);
            for (pT = pSharedseg->pStartTrace;
                 pT <= pSharedseg->pEndTrace;
                 ++pT)
               pT->tp = tpNone;
         }

      /* Allocate semaphore for mutual exclusion on trace buffer. */
      if (rc = DosCreateSem(CSEM_PUBLIC, &hssmTrace, TRACESEMNAME))
         ERRORCHK("DosCreateSem for trace sem", rc);
      DosSemClear(hssmTrace);

   }
#endif
   TRACE(tpDosInit, FALSE, 0, &pSharedseg -> bCanDOS);
}

/*****************************************************************************
 * Function to change options for the background process and/or remove
 * it.  (Removing not allowed if the NOCLOSE option was specified when
 * it was installed.)
 *****************************************************************************/
void FAR
ChangeBlanker(void)
{
   HWND            hwndDlg;
   QMSG            qmsg;

   hwndDlg = WinLoadDlg(HWND_DESKTOP, HWND_DESKTOP,
                        OptionsDlgProc, 0,
                        IDD_OPTIONS, NULL);
   if (hwndDlg == NULL)
      ERRORCHK("WinLoadDlg for options", 0);

   while (WinGetMsg(hab, &qmsg, NULL, 0, 0))
      WinDispatchMsg(hab, &qmsg);

   WinDestroyWindow(hwndDlg);
}

MRESULT CALLBACK
OptionsDlgProc(HWND hwnd, USHORT msg, MPARAM mp1, MPARAM mp2)
{
   static SHAREDSEG SharedsegCopy;
   SHORT           sTemp;
   SCRNBLNKDATA    initdata;

   switch (msg) {

   case WM_INITDLG:

      /* Centre the dialog box on the screen. */
      CentreWindow(hwnd);

      /* Remember current options. */
      SharedsegCopy = *pSharedseg;

      /* Set initial values. */
      WinSendDlgItemMsg(hwnd, IDD_SHOWCLOCK, BM_SETCHECK,
                        MPFROMSHORT(SharedsegCopy.bShowClock), NULL);
      WinSendDlgItemMsg(hwnd, IDD_PMSESS, BM_SETCHECK,
                        MPFROMSHORT(SharedsegCopy.bPM), NULL);
      WinSendDlgItemMsg(hwnd, IDD_NONPMSESS, BM_SETCHECK,
                        MPFROMSHORT(SharedsegCopy.bNonPM), NULL);
      /* Set the DOS Sessions box.  Disable it if we don't
       * have a driver, otherwise set state according current option
       * setting. */
      if (pSharedseg -> bCanDOS)
         WinSendDlgItemMsg(hwnd, IDD_DOSSESS, BM_SETCHECK,
                        MPFROMSHORT(SharedsegCopy.bDOS), NULL);
      else
         /* Disable it */
         WinEnableWindow(WinWindowFromID(hwnd, IDD_DOSSESS), FALSE);

      /* Post myself a message to set the colour button enable state. */
      WinPostMsg(hwnd, WM_CONTROL, MPFROM2SHORT(IDD_SHOWCLOCK, BN_CLICKED),
                 NULL);

      WinSetDlgItemShort(hwnd, IDD_TIME, SharedsegCopy.iTimeout / 60, FALSE);

      /* Change text on first button if we're not installed yet. */
      if (pSharedseg->pidBlanker == -1) {
         char            sz[10];
         WinLoadString(hab, 0, IDS_INSTALL, sizeof(sz) - 1, sz);
         WinSetWindowText(WinWindowFromID(hwnd, DID_OK), sz);
         /* Disable the remove button */
         WinEnableWindow(WinWindowFromID(hwnd, IDD_REMOVE), FALSE);
      }

#if defined(CMD)
      /* Set the timeout command check box, except if no command is
       * specified this is disabled. */
      if (SharedsegCopy.szTimeoutCmd[0] != '\0')
         WinSendDlgItemMsg(hwnd, IDD_RUN, BM_SETCHECK,
                           MPFROMSHORT(SharedsegCopy.bRun), NULL);
      else
         WinEnableWindow(WinWindowFromID(hwnd, IDD_RUN), FALSE);
#endif

      /* If no close was specified, disable all the affected options. */
      if (pSharedseg->bNoClose) {
         WinEnableWindow(WinWindowFromID(hwnd, IDD_REMOVE), FALSE);
         WinEnableWindow(WinWindowFromID(hwnd, IDD_PMSESS), FALSE);
         WinEnableWindow(WinWindowFromID(hwnd, IDD_DOSSESS), FALSE);
         WinEnableWindow(WinWindowFromID(hwnd, IDD_NONPMSESS), FALSE);
         WinEnableWindow(WinWindowFromID(hwnd, IDD_TIME), FALSE);
#if defined(CMD)
         WinEnableWindow(WinWindowFromID(hwnd, IDD_RUN), FALSE);
         WinEnableWindow(WinWindowFromID(hwnd, IDD_RUNSETUP), FALSE);
#endif
      }

      return FALSE;

   case WM_CONTROL:
      /* Control messages. */
      switch (SHORT1FROMMP(mp1)) {

      case IDD_SHOWCLOCK:
         /* Get the current setting. */
         SharedsegCopy.bShowClock =
            (BOOL) (LONG) WinSendDlgItemMsg(hwnd, IDD_SHOWCLOCK,
                                            BM_QUERYCHECK, NULL, NULL);
         /* Enable/disable the colours button */
         WinEnableWindow(WinWindowFromID(hwnd, IDD_COLORS),
            SharedsegCopy.bShowClock);
         return 0;

#if defined(CMD)
     case IDD_RUN:
         /* Autocheckbox for run command. Need to keep track of
          * this control for possible resetting. */
         if (SHORT2FROMMP(mp1) == BN_CLICKED)
            SharedsegCopy.bRun = ! SharedsegCopy.bRun;
         return 0;
#endif

      default:
         return 0;
      }

   case WM_COMMAND:
      switch (COMMANDMSG(&msg)->cmd) {
      case DID_OK:
         /* Check the blanking time value is a valid number */
         if (WinQueryDlgItemShort(hwnd, IDD_TIME, &sTemp, FALSE)
             && sTemp > 0
             && sTemp <= 60)
            SharedsegCopy.iTimeout = sTemp * 60;
         else {
            MessageBox(hwnd, IDS_INVTIME, MB_OK | MB_ICONEXCLAMATION);
            WinSetFocus(HWND_DESKTOP, WinWindowFromID(hwnd, IDD_TIME));
            return 0;
         }

         /*
          * Get final settings of PM, DOS and Non-PM checks
          */
         SharedsegCopy.bPM =
            (BOOL) (LONG) WinSendDlgItemMsg(hwnd, IDD_PMSESS,
                                            BM_QUERYCHECK, NULL, NULL);
         SharedsegCopy.bNonPM =
            (BOOL) (LONG) WinSendDlgItemMsg(hwnd, IDD_NONPMSESS,
                                            BM_QUERYCHECK, NULL, NULL);
         if (pSharedseg -> bCanDOS)
            SharedsegCopy.bDOS =
               (BOOL) (LONG) WinSendDlgItemMsg(hwnd, IDD_DOSSESS,
                                            BM_QUERYCHECK, NULL, NULL);
#if defined(CMD)
         /* Get final setting of the "run timeout program", if we have a
          * command defined.  */
         if (SharedsegCopy.szTimeoutCmd[0] != '\0')
            SharedsegCopy.bRun =
               (BOOL) (LONG) WinSendDlgItemMsg(hwnd, IDD_RUN,
                  BM_QUERYCHECK, NULL, NULL);
#endif

         /*
          * Get final setting of the "save settings" option, and do it if
          * requested.
          */
         if (WinSendDlgItemMsg(hwnd, IDD_PERM, BM_QUERYCHECK, NULL, NULL)) {
            /* Need to save settings in os2.ini file. */
            initdata.usVersion = VERSION;
            initdata.iTimeout = SharedsegCopy.iTimeout;
            initdata.bShowClock = SharedsegCopy.bShowClock;
            initdata.bPM = SharedsegCopy.bPM;
            initdata.bNonPM = SharedsegCopy.bNonPM;
            initdata.bDOS = SharedsegCopy.bDOS;
            initdata.colours = SharedsegCopy.colours;
#if defined(CMD)
            initdata.iWarning = SharedsegCopy.iWarning;
            initdata.iRun = SharedsegCopy.iRun;
            initdata.bRun = SharedsegCopy.bRun;
            initdata.bBeep = SharedsegCopy.bBeep;
            initdata.bClose = SharedsegCopy.bClose;
            /* Store the command and message strings. */
            if (!WinWriteProfileString(hab, "SCRNBLNK", "WARNING",
                  SharedsegCopy.szWarning))
               ERRORCHK("WinWriteProfileString(WARNING)", 0);
            if (!WinWriteProfileString(hab, "SCRNBLNK", "TIMEOUTCMD",
                  SharedsegCopy.szTimeoutCmd))
               ERRORCHK("WinWriteProfileString(TIMEOUTCMD)", 0);
#endif

            if (!WinWriteProfileData(hab, "SCRNBLNK", "OPTIONS", &initdata,
                                     sizeof(initdata)))
               ERRORCHK("WinWriteProfileData", 0);
         }

         /* Copy it all back to the shared segment */
         *pSharedseg = SharedsegCopy;
         pSharedseg->bWithdraw = FALSE;
         break;

      case DID_CANCEL:
         break;

      case IDD_REMOVE:
         /* Set the flags to un-install. */
         pSharedseg->bWithdraw = TRUE;
         /* Signal the background process. */
         DosSemClear(&pSharedseg->semMain2Back);
         break;

      case IDD_COLORS:
         /* Register the clock class here. */
         WinRegisterClass(hab, "CLOCK", ShowClockWndProc, CS_SAVEBITS,
                          sizeof(PCOLOURS));
         WinDlgBox(HWND_DESKTOP, hwnd, ColorsDlgProc,
                   0, IDD_COLORS, &SharedsegCopy.colours);
         /* Turn off the DEFAULT bit.  We don't want this to be the default */
         WinSendDlgItemMsg(hwnd, IDD_COLORS, BM_SETDEFAULT, FALSE, NULL);
         return 0L;

#ifdef CMD
      case IDD_RUNSETUP: {
         BOOL bCheck;
         WinDlgBox(HWND_DESKTOP, hwnd, RunSetupDlgProc,
                   0, IDD_RUNSETUP, &SharedsegCopy);
         /* Turn off the DEFAULT bit.  We don't want this to be the default */
         WinSendDlgItemMsg(hwnd, IDD_RUNSETUP, BM_SETDEFAULT, FALSE, NULL);
         /* Set the "enabled" state of the timeout command check box,
          * and reset the value of the check. */
         WinEnableWindow(WinWindowFromID(hwnd, IDD_RUN),
            (SharedsegCopy.szTimeoutCmd[0] != '\0'));
         bCheck = SharedsegCopy.bRun && SharedsegCopy.szTimeoutCmd[0];
         WinSendDlgItemMsg(hwnd, IDD_RUN, BM_SETCHECK,
            MPFROMSHORT(bCheck), NULL);

         return 0L;
         }
#endif
      }
      /* For SET/REMOVE/CANCEL merge here to close the window. */
      WinPostMsg(hwnd, WM_QUIT, 0L, 0L);
      return 0L;

   case WM_HELP:
      /* Display help dialog box. */
      WinDlgBox(HWND_DESKTOP, hwnd, (PFNWP) WinDefDlgProc, 0,
                (pSharedseg->pidBlanker == -1) ? IDD_HELP2 : IDD_HELP,
                NULL);
      return 0L;
   }

   return WinDefDlgProc(hwnd, msg, mp1, mp2);

}

/*
 * Function to centre a window on the screen.
 */
void NEAR
CentreWindow(HWND hwnd)
{
   RECTL           rclScreenRect;
   RECTL           rclWindowRect;
   SHORT           sWidth, sHeight, sBLCx, sBLCy;

   /* Get the screen rectangle */
   WinQueryWindowRect(HWND_DESKTOP, &rclScreenRect);

   /* Get the client rectangle */
   WinQueryWindowRect(hwnd, &rclWindowRect);

   /* Get frame window dimensions */
   sWidth = (SHORT) (rclWindowRect.xRight - rclWindowRect.xLeft);
   sHeight = (SHORT) (rclWindowRect.yTop - rclWindowRect.yBottom);

   /* Compute frame position. */
   sBLCx = (SHORT) (rclScreenRect.xRight - sWidth) / 2;
   sBLCy = (SHORT) (rclScreenRect.yTop - sHeight) / 2;

   /* Move the window. */
   WinSetWindowPos(hwnd, HWND_TOP, sBLCx, sBLCy, 0, 0, SWP_MOVE);
}

/*****************************************************************************
 * Dialog proc to change colour used for time/date display.
 *****************************************************************************/

MRESULT CALLBACK
ColorsDlgProc(HWND hwnd, USHORT msg, MPARAM mp1, MPARAM mp2)
{
   int             i;
   static PCOLOURS pcolsOld;
   static COLOURS  colsNew;

   switch (msg) {
   case WM_INITDLG:
      /* Remember our temp psect. */
      pcolsOld = PVOIDFROMMP(mp2);  /* passed current colours as param */
      colsNew = *pcolsOld;
      /* Set the range and position of the three scroll bars. */
      for (i = 0; i < 3; ++i)
         WinSendDlgItemMsg(hwnd, IDD_COLBACK + i, SBM_SETSCROLLBAR,
                           MPFROMSHORT(COLOUR(colsNew, i)),
                           MPFROM2SHORT(0, 15));
      /* Pass colour info through to ShowClockWindowProc */
      WinSetWindowULong(WinWindowFromID(hwnd, IDD_CLOCK), 0,
                        (ULONG) (PCOLOURS) & colsNew);
      return 0L;

   case WM_HSCROLL:{
      SHORT           col;
      BOOL            bSetPos;

      i = SHORT1FROMMP(mp1) - IDD_COLBACK;   /* which scroll bar */
      col = COLOUR(colsNew, i);  /* current value */
      bSetPos = TRUE;
      switch (SHORT2FROMMP(mp2)) {
      case SB_LINELEFT:
         col--;
         break;
      case SB_PAGELEFT:
         col -= 4;
         break;
      case SB_LINERIGHT:
         col++;
         break;
      case SB_PAGERIGHT:
         col += 4;
         break;
      case SB_SLIDERTRACK:
         /*
          * For this, we don't set the position.  It messes up the scroll
          * bar painting.
          */
         bSetPos = FALSE;
         col = SHORT1FROMMP(mp2);
         break;
      case SB_SLIDERPOSITION:
         /* Now we know the final position and must set it. */
         col = SHORT1FROMMP(mp2);
         break;
      default:
         return 0L;          /* nothing to do. */
      }
      /* Keep value in limits . */
      COLOUR(colsNew, i) = (UCHAR) (col = max(0, min(15, col)));
      if (bSetPos) {
         /* Set the slider position. */
         WinSendDlgItemMsg(hwnd, IDD_COLBACK + i, SBM_SETPOS,
                           MPFROMSHORT(col), NULL);
      }
      }
      /* Repaint the sample */
      WinInvalidateRect(WinWindowFromID(hwnd, IDD_CLOCK), NULL, FALSE);
      return 0L;

   case WM_COMMAND:
      switch (COMMANDMSG(&msg)->cmd) {
      case DID_OK:
         /* Copy the colours back to the shared segment copy */
         *pcolsOld = colsNew;
         /* Fall through to default processing to dismiss. */
         break;
      }
      break;
   }
   return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

/*
 * Windowproc whose job is to draw the "clock" using the current colours.
 * Pointer to colours is found in the window ULONG space.
 */
MRESULT CALLBACK
ShowClockWndProc(HWND hwnd, USHORT msg, MPARAM mp1, MPARAM mp2)
{
   static BYTE     bBlank[2] = {' ', 0};
   HDC             hdc;
   static HVPS     hvps;
   HPS             hps;
   static USHORT   usTimeLen;
   static USHORT   usDisplay;
   DTINFO          dtinfo;
   VIOCONFIGINFO   vioin;

   switch (msg) {
   case WM_CREATE:
      hdc = WinOpenWindowDC(hwnd);
      VioCreatePS(&hvps, 7, 40, 0, FORMAT_CGA, 0);
      VioAssociate(hdc, hvps);

      /* Get the display type from the config info */
      vioin.cb = sizeof(vioin);
      VioGetConfig(0, &vioin, hvps);
      usDisplay = vioin.display;

      /* Start a timer. */
      WinStartTimer(hab, hwnd, TIMERID, 1000);
      usTimeLen = 0;            /* no time yet. */
      return 0;

   case WM_TIMER:
      /* just repaint */
      WinInvalidateRect(hwnd, NULL, FALSE);
      break;

   case WM_SIZE:
      WinDefAVioWindowProc(hwnd, msg, mp1, mp2);
      break;

   case WM_QUERYDLGCODE:
      return (MRESULT) (DLGC_STATIC | DLGC_TABONCLICK);

   case WM_PAINT:{
      COLOURS         colMapped;
      PCOLOURS        pcolWanted;
      /* Get the colours from the window */
      pcolWanted = (PCOLOURS) WinQueryWindowULong(hwnd, 0);

      /* If we have a monochrome monitor, have to map the colours. */
      if (usDisplay == MONITOR_MONOCHROME || usDisplay == MONITOR_8503) {
         colMapped.colBackground = ColourMap[pcolWanted->colBackground];
         colMapped.colFrame = ColourMap[pcolWanted->colFrame];
         colMapped.colText = ColourMap[pcolWanted->colText];
      }
      else
         colMapped = *pcolWanted;

      hps = WinBeginPaint(hwnd, NULL, NULL);
      /*
       * The strange casts on the following are to suppress the SS!=DS
       * warnings; in spite of what the compiler thinks, SS is = DS.
       */
      InitClock((NPDTINFO) (DTINFO _based(_segname("_DATA")) *) & dtinfo, &colMapped);
      BuildClock(&dtinfo);
      /*
       * If the length of the time string changed (which is always the
       * case first time through), we have to resize and recentre and
       * clear our window.
       */
      if (dtinfo.usLen != usTimeLen) {
         SHORT           cyChar, cxChar;
         SWP             swpControl;
         RECTL           rclDialog;
         SHORT           sWidth;

         usTimeLen = dtinfo.usLen;  /* for next time */
         VioScrollUp(0, 0, -1, -1, -1, bBlank, hvps); /* clear window */

         /*
          * Length of window required is usLen + 2 (frame) + 4 (border)
          * chars.
          */
         VioGetDeviceCellSize(&cyChar, &cxChar, hvps);
         sWidth = cxChar * (usTimeLen + 2 + 4);

         /* Get the owner (dialog box) rectangle and current position */
         WinQueryWindowRect(WinQueryWindow(hwnd, QW_OWNER, FALSE), &rclDialog);
         /* Get my current window size and position. */
         WinQueryWindowPos(hwnd, &swpControl);
         /* Compute new position. */
         swpControl.x = (SHORT) (rclDialog.xRight - sWidth) / 2;
         swpControl.cx = sWidth;
         /* Move there... */
         swpControl.fs = SWP_MOVE | SWP_SIZE;
         WinSetMultWindowPos(hab, &swpControl, 1);
      }

      DrawClock(&dtinfo, 2, 2, hvps);
      VioShowBuf(0, 2 * 7 * 40, hvps);
      WinEndPaint(hps);
      return 0;
      }

   case WM_DESTROY:
      VioAssociate(NULL, hvps);
      VioDestroyPS(hvps);
      /* Stop my timer */
      WinStopTimer(hab, hwnd, TIMERID);
      return 0;

   }
   return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

#ifdef CMD
/*****************************************************************************
 * Dialog procedure to setup the timeout command options.
 *****************************************************************************/

MRESULT CALLBACK
RunSetupDlgProc(HWND hwnd, USHORT msg, MPARAM mp1, MPARAM mp2)
{
   static PSHAREDSEG pSharedsegCopy;
   SHORT sTemp;

   switch (msg) {
   case WM_INITDLG:
      /* Remember the psect. */
      pSharedsegCopy = PVOIDFROMMP(mp2);  /* passed as parameter. */

      /* Set the max length for the two string controls. */
      WinSendDlgItemMsg(hwnd, IDD_RUNWARNMESS, EM_SETTEXTLIMIT,
         MPFROMSHORT(MAXWARNLEN), NULL);
      WinSendDlgItemMsg(hwnd, IDD_RUNCMD, EM_SETTEXTLIMIT,
         MPFROMSHORT(CCHMAXPATH), NULL);

      /* Set the check boxes and the strings. */
      WinSendDlgItemMsg(hwnd, IDD_RUNBEEP, BM_SETCHECK,
                           MPFROMSHORT(pSharedsegCopy->bBeep), NULL);
      WinSendDlgItemMsg(hwnd, IDD_CLOSEWIN, BM_SETCHECK,
                           MPFROMSHORT(pSharedsegCopy->bClose), NULL);
      WinSetDlgItemShort(hwnd, IDD_RUNWARNTIME, pSharedsegCopy->iWarning/60,
                           FALSE);
      WinSetDlgItemShort(hwnd, IDD_RUNTIME, pSharedsegCopy->iRun/60,
                           FALSE);
      WinSetDlgItemText(hwnd, IDD_RUNWARNMESS, pSharedsegCopy->szWarning);
      WinSetDlgItemText(hwnd, IDD_RUNCMD, pSharedsegCopy->szTimeoutCmd);
      return 0L;

   case WM_COMMAND:
      switch (COMMANDMSG(&msg)->cmd) {
      case DID_OK:
         /* Retrieve current settings of all the check boxes and
          * strings. */
         pSharedsegCopy -> bBeep =
            (BOOL) (LONG)  WinSendDlgItemMsg(hwnd, IDD_RUNBEEP,
                                             BM_QUERYCHECK, NULL, NULL);
         pSharedsegCopy -> bClose =
            (BOOL) (LONG)  WinSendDlgItemMsg(hwnd, IDD_CLOSEWIN,
                                             BM_QUERYCHECK, NULL, NULL);

         if (WinQueryDlgItemShort(hwnd, IDD_RUNTIME, &sTemp, FALSE)
               && sTemp > 0
               && sTemp <= 120)
            pSharedsegCopy->iRun = sTemp * 60;
         else {
            MessageBox(hwnd, IDS_INVRUNTIME, MB_OK | MB_ICONEXCLAMATION);
            WinSetFocus(HWND_DESKTOP, WinWindowFromID(hwnd, IDD_RUNTIME));
            return 0;
         }
         if (WinQueryDlgItemShort(hwnd, IDD_RUNWARNTIME, &sTemp, FALSE)
               && sTemp > 0
               && sTemp < pSharedsegCopy->iRun/60)
            pSharedsegCopy->iWarning = sTemp * 60;
         else {
            MessageBox(hwnd, IDS_INVWARNTIME, MB_OK | MB_ICONEXCLAMATION);
            WinSetFocus(HWND_DESKTOP, WinWindowFromID(hwnd, IDD_RUNWARNTIME));
            return 0;
         }
         WinQueryDlgItemText(hwnd, IDD_RUNWARNMESS, MAXWARNLEN,
            pSharedsegCopy->szWarning);
         WinQueryDlgItemText(hwnd, IDD_RUNCMD, CCHMAXPATH,
            pSharedsegCopy->szTimeoutCmd);
         break;
      }
      break;

   case WM_HELP:
      /* Display help dialog box. */
      WinDlgBox(HWND_DESKTOP, hwnd, (PFNWP) WinDefDlgProc, 0,
                IDD_HELP3, NULL);

   }
   return WinDefDlgProc(hwnd, msg, mp1, mp2);
}
#endif

/******************************************************************************
 * Routine to popup a little dialog box with a message and remove it after
 * a couple of seconds.
 * Note there is no window when this is called, but there is a message
 * queue already..
 *****************************************************************************/

void FAR
PopupBox(USHORT idDlg)
{
   HWND            hwndDlg;

   hwndDlg = WinLoadDlg(HWND_DESKTOP, HWND_DESKTOP, PopupDlgProc,
                        0, idDlg, NULL);
   if (hwndDlg == NULL)
      ERRORCHK("WinLoadDlg", 0);
   WinStartTimer(hab, hwndDlg, TIMERID, 2000);
   WinProcessDlg(hwndDlg);
   WinStopTimer(hab, hwndDlg, TIMERID);
   WinDestroyWindow(hwndDlg);
}

MRESULT CALLBACK
PopupDlgProc(HWND hwnd, USHORT msg, MPARAM mp1, MPARAM mp2)
{
   switch (msg) {
      case WM_TIMER:
      case WM_CHAR:
      case WM_BUTTON1DOWN:
      WinDismissDlg(hwnd, TRUE);
      return 0;
   }
   return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

/*
 * MessageBox uses WinMessageBox to display a message specified by a
 * string resource.
 */
void FAR
MessageBox(HWND hwnd, USHORT idRes, USHORT flStyle)
{
   char            szMsg[255];
   WinLoadString(hab, 0, idRes, sizeof(szMsg) - 1, szMsg);
   WinMessageBox(HWND_DESKTOP,
                 hwnd,
                 szMsg,
                 NULL,
                 -1,
                 flStyle);
}

/*****************************************************************************
 *  CriticalError uses VioPopUp to display an error message, obtained from
 *  the resource file.
 *  Message is a series of consecutive string resources, terminated by
 *  a null string.
 *  Used when it isn't safe to use a PM window.  (Currently, during startup
 *  only.)
 *****************************************************************************/
void FAR
CriticalError(USHORT idRes)
{
   USHORT          fWait = VP_WAIT | VP_OPAQUE;
   KBDKEYINFO      kbci;
   int             iLen;
   int             rc;
   char            szMsg[255];

   DosBeep(800, 333);
   while ((rc = VioPopUp(&fWait, 0)) == ERROR_VIO_SHELL_INIT)
      /* Shell not started yet.  Wait a moment. */
      DosSleep(1000L);
   if (rc != 0) {
      DosBeep(1200, 333);
      return;
   }
   while (iLen = WinLoadString(hab, 0, idRes, sizeof(szMsg) - 1, szMsg)) {
      WtiLStrCpy(szMsg + iLen, "\n\r");
      iLen += 2;
      VioWrtTTY(szMsg, iLen, 0);
      ++idRes;
   }

   /* Wait for a key to be pressed. */
   KbdCharIn(&kbci, IO_WAIT, 0);
   VioEndPopUp(0);
}

/*
 *  ErrorPopUp uses VioPopUp to display an error message, then terminates
 *  the process.  Used for internal problems.  In cases where it can't
 *  get the popup, it issues a beep. It may be called from any of the
 *  processes constituting the blanker.
 */
void FAR
ErrorPopUp(PSZ pszMessage, int retc)
{
   USHORT          fWait = VP_NOWAIT | VP_OPAQUE;
   char            szMess[100];
   int             iLen;
   KBDKEYINFO      kbci;
   ULONG           sem;
   int             rc;

   /* This popup doesn't wait, but keep retrying.  This is so that
    * it has a chance of working OK for problems in the monitor process
    * even if the main process has the screen blanked. */
   while ((rc = VioPopUp(&fWait, 0)) != 0) {
      /* Beep, so even if popup etc. fails, we have some indication of
       * a problem. */
      DosBeep(800, 333);

      /* Fake an action, if the main process has the popup.
       * This only does any good if we're the monitor. */
      if (pSharedseg != NULL && pSharedseg->bBlanked)
         DosSemClear(&pSharedseg->semAction);
      /* Try ending the popup, in case we're the process that has it. */
      rc = VioEndPopUp(0);

      DosSleep(500L);  /* wait half a sec */
   }
   iLen = sprintf(szMess, "\n\nSCRNBLNK error: %Fs error code %d\n\n\rPress any key ...",
                  pszMessage, retc);
   VioWrtTTY(szMess, iLen, 0);


   KbdCharIn(&kbci, IO_WAIT, 0);

   VioEndPopUp(0);
#if defined(DEBUG)
   /* Hang around so we can use peek to see what happened */
   if (pSharedseg != NULL && pSharedseg->bDebug)
      DosSemSetWait(&sem, 1000L * 60 * 10);  /* 10 minute wait */
#endif
   DosExit(EXIT_PROCESS, 1);
}
