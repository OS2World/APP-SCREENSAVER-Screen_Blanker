/*****************************************************************************
 * OS/2 Screen Blanker.
 * This file contains the time display segment.
 *
 * Copyright (c) 1991 Ballard Consulting
 * Alan Ballard
 *****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INCL_DOSINFOSEG
#define INCL_DOSDATETIME
#define INCL_DOSSEMAPHORES
#define INCL_DOSNLS
#define INCL_VIO
#define INCL_DOSERRORS
#define INCL_AVIO
#define INCL_SHELLDATA
#define INCL_WINSHELLDATA
#include <os2.h>
#include <os2misc.h>

#include "scrnblnk.h"
#include "scrndlg.h"

/* Special characters used in time-date display */
#define BOX_HORIZ '\xcd'
#define BOX_VERT  '\xba'
#define BOX_TL    '\xc9'
#define BOX_TR    '\xbb'
#define BOX_BL    '\xc8'
#define BOX_BR    '\xbc'

#define COL_BLANK  0
#define ATTR_BLANK (COL_BLANK * 16 + COL_BLANK)
USHORT NEAR     MakeTime(NPDTINFO npdtinfo, NPSZ npszDate);
void NEAR       GetTimeInfo(NPTIMEINFO);

/* Anchor block is set globally by main program */
extern HAB      hab;
/* Pointer to system global information */
extern PGINFOSEG pGInfo;

/******************************************************************************/
/*
 *  InitClock initializes the clock display data structures.
 */
void FAR
InitClock(NPDTINFO npdtinfo, PCOLOURS pcolours)
{
   /* Set dummy values for first time through */
   npdtinfo->usLen = 0;
   npdtinfo->LastMonth = 13;    /* invalid */
   npdtinfo->LastDay = 7;       /* invalid */
   npdtinfo->LastMinutes = 60;  /* invalid */
   npdtinfo->ulLastMoveTime = 0;

   /* set attributes needed */
   npdtinfo->bBox = (BYTE) 16 *pcolours->colBackground +
                   pcolours->colFrame;
   npdtinfo->bDate = (BYTE) 16 *pcolours->colBackground +
                   pcolours->colText;

   GetTimeInfo(&npdtinfo->timei);

}

/*
 * Get the formatted time string, and rebuild the box if necessary.
 * Returns the length of the time string.
 */
void FAR
BuildClock(NPDTINFO npdtinfo)
{
   USHORT          usLen;

   usLen = MakeTime(npdtinfo, npdtinfo->achBoxBody + 1);
   if (usLen != npdtinfo->usLen) {
      /* Have to rebuild the box to the required length */
      npdtinfo->achBoxTop[0] = BOX_TL;
      memset(npdtinfo->achBoxTop + 1, BOX_HORIZ, usLen);
      npdtinfo->achBoxTop[usLen + 1] = BOX_TR;

      npdtinfo->achBoxBot[0] = BOX_BL;
      memset(npdtinfo->achBoxBot + 1, BOX_HORIZ, usLen);
      npdtinfo->achBoxBot[usLen + 1] = BOX_BR;

      npdtinfo->achBoxBody[0] = BOX_VERT;
      npdtinfo->usLen = usLen;
   }
   npdtinfo->achBoxBody[usLen + 1] = BOX_VERT;

}

/*
 *  Display the clock at the specified position, with the specified
 *  presentation space.
 */
void FAR
DrawClock(NPDTINFO npdtinfo, USHORT xPos, USHORT yPos, HVPS hvps)
{
   USHORT          usLen;
   usLen = npdtinfo->usLen;

   /* First draw all three lines of the box. */
   VioWrtCharStrAtt(npdtinfo->achBoxTop,
                    usLen + 2, yPos, xPos, &npdtinfo->bBox, hvps);
   VioWrtCharStrAtt(npdtinfo->achBoxBody,
                    usLen + 2, yPos + 1, xPos, &npdtinfo->bBox, hvps);
   VioWrtCharStrAtt(npdtinfo->achBoxBot,
                    usLen + 2, yPos + 2, xPos, &npdtinfo->bBox, hvps);

   /* Now reset the attributes on the date/time string */
   VioWrtNAttr(&npdtinfo->bDate, usLen, yPos + 1, xPos + 1, hvps);
}

/*
 * ShowTime computes the current datetime string and displays it.
 */
void FAR
ShowTime(NPDTINFO npdtinfo)
{
   USHORT          usPrevLen;
   USHORT          xRand, yRand;
   USHORT          minutes;
   static BYTE     bBlank[2] = {' ', ATTR_BLANK};
   BOOL            bWriteAll;

   /* Make sure all minutes references are consistent. */
   minutes = pGInfo->minutes;

   /*
    * Check for very fast update when only seconds have changed. (We assume
    * here that we'll never go a whole hour without getting activated...)
    */
   if (minutes == npdtinfo->LastMinutes &&
       pGInfo->time < npdtinfo->ulLastMoveTime + 10) {
      USHORT          secs;
      char            chsecs[2];
      secs = pGInfo->seconds;   /* make sure we get consistent digits */
      chsecs[0] = (char) ((int) '0' + secs / 10);
      chsecs[1] = (char) ((int) '0' + secs % 10);
      VioWrtCharStr(chsecs, 2,
                    npdtinfo->yLast + 1,
                    npdtinfo->xLast + 1 + npdtinfo->xSeconds,
                    0);
      return;
   }

   /* Get the time, rebuilding the box if necessary */
   usPrevLen = npdtinfo->usLen;
   BuildClock(npdtinfo);

   /* See if it is time to move the display. */
   if (pGInfo->time > npdtinfo->ulLastMoveTime + 10) {
      /* Compute new random position */
      xRand = rand() % (npdtinfo->xScreen - npdtinfo->usLen - 2);
      yRand = rand() % (npdtinfo->yScreen - 3);
      npdtinfo->ulLastMoveTime = pGInfo->time;

      bWriteAll = TRUE;
   }
   else {
      /* Use old position */
      xRand = npdtinfo->xLast;
      yRand = npdtinfo->yLast;

      bWriteAll = FALSE;
   }

   if (bWriteAll || usPrevLen != npdtinfo->usLen) {
      /* Blank the current position */
      VioScrollUp(npdtinfo->yLast,
                  npdtinfo->xLast,
                  npdtinfo->yLast + 3,
                  npdtinfo->xLast + usPrevLen + 2,
                  3,
                  bBlank,
                  0);

      /* And repaint it in the new one. */
      DrawClock(npdtinfo, xRand, yRand, 0);

   }
   else
      /* Just rewrite the date/time string part. */
      VioWrtCharStr(npdtinfo->achBoxBody + 1,
                    npdtinfo->usLen, yRand + 1, xRand + 1, 0);

   /* Remember when/where we displayed it. */
   npdtinfo->xLast = xRand;
   npdtinfo->yLast = yRand;
   npdtinfo->LastMinutes = minutes;
}

/*
 * MakeTime builds the time value in the passed string variable.
 * Returns actual length of the time string.
 */
USHORT NEAR
MakeTime(NPDTINFO npdtinfo, NPSZ npszDate)
{
   USHORT          usLen;

   /*
    * Get the weekday and monthname from the resource file, if they are
    * needed.
    */
   if ((USHORT) pGInfo->weekday != npdtinfo->LastDay)
      WinLoadString(hab, 0, IDS_SUNDAY + pGInfo->weekday,
                    MAX_NAMELEN, npdtinfo->szDayName);
   if ((USHORT) pGInfo->month != npdtinfo->LastMonth)
      WinLoadString(hab, 0, IDS_JANUARY + pGInfo->month - 1,
                    MAX_NAMELEN, npdtinfo->szMonthName);

   /* First the day name followed by d/m/y in one of three orders. */
   if (npdtinfo->timei.fsDateFmt == DATEFMT_MM_DD_YY)
      usLen = sprintf(npszDate, " %s %s %2d, %4d ",
                      npdtinfo->szDayName,
                      npdtinfo->szMonthName,
                      pGInfo->day,
                      pGInfo->year);

   else if (npdtinfo->timei.fsDateFmt == DATEFMT_DD_MM_YY)
      usLen = sprintf(npszDate, " %s %2d %s %4d ",
                      npdtinfo->szDayName,
                      pGInfo->day,
                      npdtinfo->szMonthName,
                      pGInfo->year);

   else                         /* year month day */
      usLen = sprintf(npszDate, " %s %4d %s %2d ",
                      npdtinfo->szDayName,
                      pGInfo->year,
                      npdtinfo->szMonthName,
                      pGInfo->day);

   /* Now the time in one of two formats. */
   if ((npdtinfo->timei.fsTimeFmt & 1) == 0) {
      /* 12 hour format */
      UCHAR           hour;
      hour = pGInfo->hour;      /* make sure consistent */
      usLen += sprintf(npszDate + usLen, "%2d%s%02d%s%02d ",
                       (hour + 11) % 12 + 1,
                       npdtinfo->timei.szTimeSeparator,
                       pGInfo->minutes,
                       npdtinfo->timei.szTimeSeparator,
                       pGInfo->seconds);
      npdtinfo->xSeconds = usLen - 3;  /* record seconds pos */
      /* Append the AM/PM */
      usLen += sprintf(npszDate + usLen, "%s ",
                   hour / 12 ? npdtinfo->timei.szPM : npdtinfo->timei.szAM);

   }
   else {
      /* 24 hour format */
      usLen += sprintf(npszDate + usLen, "%02d%s%02d%s%02d ",
                       pGInfo->hour,
                       npdtinfo->timei.szTimeSeparator,
                       pGInfo->minutes,
                       npdtinfo->timei.szTimeSeparator,
                       pGInfo->seconds);
      npdtinfo->xSeconds = usLen - 3;  /* remember seconds pos */
   }
   return usLen;
}

/*
 * GetTimeInfo gets the country information from Dos, then updates it
 * with the control panel info.  Retrieves only those pieces
 * relevant to time/date format that we need.
 */
void NEAR
GetTimeInfo(NPTIMEINFO nptimei)
{
   static COUNTRYCODE ctryc = {0, 0};
   COUNTRYINFO     ctryi;
   USHORT          usDataLength;

   DosGetCtryInfo(sizeof(COUNTRYINFO), &ctryc, &ctryi, &usDataLength);
   /* See what is in the OS2.INI file, using Dos ones as defaults. */
   nptimei->fsDateFmt = WinQueryProfileInt(hab, "PM_National", "iDate",
                                           ctryi.fsDateFmt);
   WinQueryProfileString(hab, "PM_National", "sTime",
                         ctryi.szTimeSeparator, nptimei->szTimeSeparator, 2);
   nptimei->fsTimeFmt = (UCHAR) WinQueryProfileInt(hab, "PM_National", "iTime",
                                                   ctryi.fsTimeFmt);
   WinQueryProfileString(hab, "PM_National", "s1159", "am",
                         nptimei->szAM, 3);
   WinQueryProfileString(hab, "PM_National", "s2359", "pm",
                         nptimei->szPM, 3);
}
