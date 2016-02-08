/******************************************************************************
 * OS/2 Screen Blanker.
 * This file contains the DLL routines for a windows hook for determining
 * whether there is any activity in the PM session.
 *
 * Copyright (c) 1991 Ballard Consulting
 * Alan Ballard
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>

#define INCL_DOS
#define INCL_WIN
#define INCL_AVIO
#include <os2.h>
#include <os2misc.h>
#include <pmtkt.h>

#include "scrnblnk.h"
#include "scrndlg.h"

/* No default runtime... */
int             _acrtused = 0;

/* Global variables in the data segment for the hook dll. */
static HAB      habScrnBlnk;    /* anchor block */
static HMODULE  hmod;
static ULONG    semPM;
#if defined(DEBUG)
static QMSG     qmsgLast;     /* for debugging */
#endif

/* The actual hook routine.... */
void CALLBACK   ScrnBlnkHook(HAB hab, PQMSG pQmsg);

/****************************************************************************
 * ScrnBlnkVersion just returns the current version number and is used
 * to detect situation of running with run version of the DLL.
 ****************************************************************************/
USHORT FAR PASCAL
ScrnBlnkVersion(void)
{
   return VERSION;
}

/******************************************************************************
 *
 *  ScrnBlnkInstallHook is used to install a windows hook that uses the
 *  journal record hook to notice all keystroke and mouse actions.
 *
 *  Returns address of the RAM semaphore we use to signal activity.
 ******************************************************************************/

PULONG FAR PASCAL
#if defined(DEBUG)
ScrnBlnkInstallHook(HAB hab, PQMSG FAR * ppqmsg)
#else
ScrnBlnkInstallHook(HAB hab)
#endif
{
   /* We need a handle for the current module. */
   if (DosGetModHandle("SCRNBLNK", &hmod))
      return NULL;

   /* Save anchor block in global segment. */
   habScrnBlnk = hab;

   /*
    * Set hook for journal-record, which gets all mouse and keyboard events,
    * but not other messages.
    */
   if (!WinSetHook(hab, (HMQ) 0, HK_JOURNALRECORD, (PFN) ScrnBlnkHook, hmod))
      return NULL;

#if defined(DEBUG)
   /* Return pointer to place we stash last message */
   *ppqmsg = &qmsgLast;
#endif

   /* Return semaphore address. */
   return &semPM;
}

/******************************************************************************
 *
 *  ScrnBlnkReleaseHook is used to remove the hook.
 *
 ******************************************************************************/
BOOL FAR PASCAL
ScrnBlnkReleaseHook(void)
{
   return WinReleaseHook(habScrnBlnk, (HMQ) 0, HK_JOURNALRECORD,
                         (PFN) ScrnBlnkHook, hmod);
}

/******************************************************************************
 *
 *  ScrnBlnkHook is called by PM for each keystroke or mouse action in the
 *  PM session.
 *
 ******************************************************************************/
void CALLBACK
ScrnBlnkHook(HAB hab, PQMSG pQmsg)
{
#if defined(DEBUG)
   qmsgLast = *pQmsg;
#endif
   switch (pQmsg->msg) {
   case WM_MOVE:
   case WM_BUTTON1DOWN:
   case WM_BUTTON1UP:
   case WM_BUTTON1DBLCLK:
   case WM_BUTTON2DOWN:
   case WM_BUTTON2UP:
   case WM_BUTTON2DBLCLK:
   case WM_BUTTON3DOWN:
   case WM_BUTTON3UP:
   case WM_BUTTON3DBLCLK:
   case WM_VIOCHAR:
   case WM_CHAR:   
      /* Just signal that it happened. */
      DosSemClear(&semPM);
   }
}
