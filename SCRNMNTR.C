 /******************************************************************************
 * OS/2 Screen Blanker.
 * Copyright (c) 1991 Ballard Consulting
 * Alan Ballard
 *
 * This file contains the monitoring process code segment.
 * It is called from SCRNMAIN to form the keyboard and mouse monitoring
 * process.  A separate process is used rather than threads within the
 * main background process, because there is no dependable
 * way to stop a thread when the screen group changes to reregister.
 * This process is killed when the screen group changes, then restarted
 * for the new screen group.
 *
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#define INCL_DOSINFOSEG
#define INCL_DOSMEMMGR
#define INCL_DOSDATETIME
#define INCL_DOSPROCESS
#define INCL_DOSMONITORS
#define INCL_DOSSEMAPHORES
#define INCL_DOSERRORS
#define INCL_AVIO
#define INCL_WINSYS

#include <os2.h>
#include <os2misc.h>
#include <pmtkt.h>

#define STACKSIZE       (4096)  /* for monitor threads */

#include "scrnblnk.h"
#include "scrndlg.h"

void CALLBACK   MonitorThread(NPSZ);
void NEAR       StartThread(NPSZ);

/* Pointer to system global information */
extern PGINFOSEG pGInfo;
/* Screen blanker shared memory segment. */
extern PSHAREDSEG pSharedseg;
static SHORT sgMonitor;     /* session to monitor */

void FAR
ScrnMonitor(void)
{
   /* Signal starter we're here. */
   DosSemClear(&pSharedseg->semStartup);

   /* Remember what session to monitor, and check we haven't already
    * switched. This is to handle timing problems that may result in
    * trying to monitor PM or DOS sessions.
    */
   sgMonitor = pGInfo -> sgCurrent;

   if (sgMonitor == SESS_DOS || sgMonitor == SESS_PM) {
      /* Hm, this is a session we don't monitor.
       * If we are blanked, indicate some event occurred;
       * this will unblank the screen.  That is probably what we want,
       * since otherwise we end up with no monitor running and with
       * no way to unblank. */
      if (pSharedseg->bBlanked)
         DosSemClear(&pSharedseg->semAction);
      DosExit(EXIT_THREAD, 0);
   }

   /*
    * If we have a mouse, start a separate thread to monitor it.
    */
   if (pSharedseg -> bMouse)
      StartThread("MOUSE$");

   /* Continue here to monitor the keyboard. */
   MonitorThread("KBD$");
}

/*****************************************************************************
 *
 * Initialize a monitor thread.
 *
 *****************************************************************************/
void NEAR
StartThread(NPSZ npName)
{
   int             rc;
   NPULONG FAR    *pnpParam;
   TID             tid;

   /*
    * Allocate the stack. Using malloc here gets us a stack in the data
    * segment so ss=ds for all routines.
    */
   pnpParam = (NPULONG FAR *) ((char FAR *) malloc(STACKSIZE) + STACKSIZE);

   /* Push the parameter on the stack. */
   *--pnpParam = (NPULONG) npName;

   /* Create the new thread. */
   if (rc = DosCreateThread((PFNTHREAD) MonitorThread, &tid,
                            (void FAR *) pnpParam))
      ERRORCHK("DosCreateThread", rc);

}

/* This function is called from two threads, one each to monitor the
 * keyboard and mouse messages.
 * All packets are passed on unchanged.  The thread just clears a
 * semaphore to alert the main background process that there has been some
 * activity, which means it won't blank or will restore the screen if
 * it is already blanked.
 *
 * Parameter is name of device to be monitored.
 */
void CALLBACK
MonitorThread(NPSZ npName)
{
   int             rc;
   MONIN           mnin;        /* monitor buffers */
   MONOUT          mnout;
   HMONITOR        hmon;
   char            bDataBuf[sizeof(MONIN)];  /* buffer used */
   USHORT          cbDataBufLen;
   PIDINFO         pidi;

   /* It has to be time-critical, so it doesn't impact response. */
   DosGetPID(&pidi);
   DosSetPrty(PRTYS_THREAD, PRTYC_TIMECRITICAL, 0, pidi.tid);

   /* Initialize the lengths of the monitor buffer structures. */
   mnin.cb = sizeof(MONIN);
   mnout.cb = sizeof(MONOUT);

   /*
    * Register for the current session.
    */
   TRACE(tpMonOpen, TRUE, 0, npName);
   if (rc = DosMonOpen(npName, &hmon))
      ERRORCHK("DosMonOpen", rc);
   TRACE(tpMonReg, TRUE, 0, npName);
   rc = DosMonReg(hmon,
                  (PBYTE) & mnin,
                  (PBYTE) & mnout,
                  MONITOR_BEGIN, sgMonitor);
   TRACE(tpMonReg, FALSE, rc, npName);
   if (rc == 0)
      /*
       * Loop, passing on data and signalling main thread.
       */
      for (;;) {
         cbDataBufLen = sizeof(MONIN);
         if (DosMonRead((PBYTE) & mnin, DCWW_WAIT, bDataBuf, &cbDataBufLen))
            break;
         /* We shouldn't get a length 0 return, but best to check, before
          * testing the first byte... */
         if (cbDataBufLen > 0) {
            if (DosMonWrite((PBYTE) & mnout, bDataBuf, cbDataBufLen))
               break;
            /* Test for open/close/flush packets and ignore them;
             * they don't represent mouse/keyboard actions. */
            if (!(bDataBuf[0] & 0x07)) {
               TRACE(tpMonAction, FALSE, cbDataBufLen, npName);
               DosSemClear(&pSharedseg->semAction);
            }
         }
      }
   else {
      /* Failed to register */
      WtiLStrCpy(bDataBuf, "DosMonReg for ");
      WtiLStrCat(bDataBuf, npName);
      ERRORCHK(bDataBuf, rc);
   }

   /* If blanked, undo it, whatever the reason we're leaving... */
   if (pSharedseg->bBlanked)
      DosSemClear(&pSharedseg->semAction);

   /* Told to withdraw */
   DosExit(EXIT_THREAD, 0);
}
