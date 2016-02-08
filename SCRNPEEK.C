#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_AVIO
#include <os2.h>
#include <pmtkt.h>

#include "scrnblnk.h"
char *tpName[] = {"None", "Init", "StartPgm", "StartSem", "StartSG", "Stop", 
   "Mux","PopUp", "EndPopUp", "KillProcess", "MonOpen", "MonReg",
   "MonAction", "WinTerm", "MouOpen", "MouClose", "DosInit",
   "DosCheck", "WarnPopUp", "WarnEndPopUp", "WinSem", "Run"};

#undef ERRORCHK
#define ERRORCHK(msg,rc) fprintf(stderr, "%s %d\n", msg, rc)

void _cdecl main(int argc, char *argv[]);

PSHAREDSEG pSharedseg;  /* shared memory segment */
/******************************************************************************/

/*
 * main program.
 *
 */
void _cdecl main(int argc, char *argv[])
{
   int rc, c;
   SEL selSharedseg;
   TRACEENTRY FAR * pTrace;
   BOOL wait;

   wait = FALSE;
   if (argc > 1 && strcmpi(argv[1], "wait") == 0)
      wait = TRUE;
   
   if (rc = DosAllocShrSeg(sizeof(SHAREDSEG), SHAREDSEGNAME, &selSharedseg)) {
      if (rc != ERROR_ALREADY_EXISTS)
         ERRORCHK("DosAllocShrSeg for " SHAREDSEGNAME, rc);
      if (rc = DosGetShrSeg(SHAREDSEGNAME, &selSharedseg))
         ERRORCHK("DosGetShrSeg for " SHAREDSEGNAME, rc);
      pSharedseg = MAKEP(selSharedseg, 0);

   } else {
      /*  Segment didn't exist. */
      fprintf(stderr, "segment not defined\n");
      exit(1);
   }
   if (!pSharedseg -> bDebug) {
      fprintf(stderr, "SCRNBLNK not started with /DEBUG option\n");
      exit(1);
   }
   
   
   /* Get the trace buffer. */
   while ((rc = DosGetSeg(pSharedseg -> selTrace)) == 5)
      ;
   if (rc != 0)
      ERRORCHK("DosGetSeg", rc);

   if (wait) {
      fprintf(stderr, "Press ENTER to begin output...");
      c = getchar();
   }
   
   /* Print some items from the shared seg */
   printf("withdraw = %d; started=%lx main2back=%lx\n",
      pSharedseg -> bWithdraw, 
      pSharedseg -> semStartup,
      pSharedseg -> semMain2Back);   
   
   /* Print the trace, LIFO */
   pTrace = pSharedseg -> pNextTrace;
   do {
      if (pTrace == pSharedseg -> pStartTrace)
         pTrace = pSharedseg -> pEndTrace;
      else
         --pTrace;
      if (pTrace -> tp == tpNone)
         break;

      if (pTrace -> bBefore)
         printf("bef ");
      else
         printf("aft ");
      
      printf("%s rc=%d ", tpName[pTrace -> tp], pTrace -> rc);
      
      switch (pTrace -> tp) {
      case tpStartSg:
         printf("screen group=%d", *(UCHAR FAR *)&(pTrace->ulMisc));
         break;
         
      case tpMonOpen:
      case tpMonReg:
      case tpMonAction:
         printf("%.4Fs",(char FAR *)&(pTrace->ulMisc));
         break;
         
      case tpMux:
         if (!pTrace -> bBefore)
            printf("index = %d", *(USHORT FAR *) &(pTrace -> ulMisc));
         break;

      case tpDosInit:
         if (*(BOOL FAR *)&(pTrace->ulMisc))
            printf("yes");
         else
            printf("no");
         break;

      case tpDosCheck:
         printf("count = %d", *(USHORT FAR *) &(pTrace -> ulMisc));
         break;

      case tpWinSem:
         printf("time = %ld", *(ULONG FAR *) &(pTrace -> ulMisc));
         break;
         ;
      }
      printf("\n");
   } while (pTrace != pSharedseg -> pNextTrace);
   exit(0);
}
