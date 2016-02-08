# SCRNBLNK Makefile
#

#===================================================================
#
#  Standard command line definitions
#
#===================================================================

# Note we don't use -Aw even though we're a multithread program,
# because the stacks for the other threads are allocated with
# malloc and so are in our default segment.

# We use medium model, because we want more than one segment, and
# need to get at FAR library routines as a result.
#cp=cl -c -Lp -W3 -G2rs -AM -Oxz -Zpe -Fr -DDEBUG -DCMD
cp=cl -c -Lp -W3 -G2rs -AM -Oxz -Zpe -Fr -DDEBUGFILE

#===================================================================
#
#  Default inference rules
#
#===================================================================

.c.obj:
    $(cp) $*.c

.asm.obj:
    masm $*.asm;

.rc.res:
    rc -r $*.rc
 
#===================================================================
#
#  Dependencies
#
#===================================================================

all: scrnblnk.exe scrnblnk.dll scrnblnk.sys scrnpeek.exe scrnblnk.zip scrnbls.zip scrnblnk.bsc scrnhook.lib

# The scrnblnk DLL for hooking PM input
scrnhook.obj: scrnhook.c scrnblnk.h 
     $(cp) -AMu scrnhook.c 
####   cl -c -Lp -W3 -G2s -ALu -Oxa -Zpei scrnhook.c

scrnblnk.dll: scrnhook.obj scrnhook.def
   link /F scrnhook, scrnblnk.dll/ALIGN:16, NUL, os2/NOD, scrnhook.def
   
scrnhook.lib: scrnhook.def
   implib scrnhook.lib scrnhook.def

# The scrnblnk driver, used to link from dos box to OS/2 blanker  
scrnblnk.sys : scrndrvr.obj scrndrvr.def
        link scrndrvr,scrnblnk.sys,,os2,scrndrvr.def

scrndrvr.obj : scrndrvr.asm
   masm /T scrndrvr.asm;

scrnmain.obj: scrnmain.c scrnblnk.h scrndlg.h
    $(cp) -NT _MAIN scrnmain.c

scrninst.obj: scrninst.c scrnblnk.h scrndlg.h
    $(cp) -NT _INST scrninst.c

scrnback.obj: scrnback.c scrnblnk.h scrndlg.h
    $(cp) -NT _BACK scrnback.c

scrnmntr.obj: scrnmntr.c scrnblnk.h scrndlg.h
    $(cp) -NT _MNTR scrnmntr.c

scrntime.obj: scrntime.c scrnblnk.h scrndlg.h
    $(cp) -NT _TIME scrntime.c

scrnblnk.res: scrnblnk.rc scrndlg.h

scrnblnk.exe: scrnmain.obj scrnback.obj scrntime.obj scrnmntr.obj scrninst.obj scrnhook.lib scrnblnk.res
    link /F scrnmain+scrnback+scrntime+scrnmntr+scrninst,scrnblnk.exe/ALIGN:16, NUL, os2+mlibcep+scrnhook/NOD, SCRNBLNK
    rc scrnblnk.res

#scrpopup.obj: scrpopup.c scrnblnk.h
#   $(cp) -NT _POPU scrpopup.c
#
#scrpopup.exe: scrpopup.obj
#   link scrpopup,scrpopup.exe/ALIGN:16, NUL, os2+mlibcep/NOD, NUL

# scrnpeek GLP used to look at the debug trace buffer.
scrnpeek.obj : scrnpeek.c scrnblnk.h
   $(cp) scrnpeek.c

scrnpeek.exe: scrnpeek.obj
   link scrnpeek,scrnpeek.exe/ALIGN:16,NUL, os2+mlibcep/NOD, NUL

# PWB source browse stuff.
scrnblnk.bsc: scrnmain.sbr scrnback.sbr scrntime.sbr scrnhook.sbr scrnmntr.sbr scrninst.sbr
   pwbrmake /Es /o scrnblnk.bsc $**

# no dependencies shown for following, so they are only made when I 
# remove the old one.
scrnbls.zip :
   pkzip scrnbls scrnmain.c scrnback.c scrntime.c scrnhook.c  \
   scrnmntr.c scrninst.c scrnblnk.h scrndlg.h scrnblnk.rc scrnblnk.def \
   scrnhook.def scrndrvr.def scrnblx.doc scrnblnk.ico scrnblnk.mak \
   scrnpeek.c scrndrvr.asm technote.txt os2misc.h pmtkt.h readme scrnblnk.res scrnblx.txt

scrnblnk.zip :
  pkzip scrnblnk scrnblnk.exe scrnblnk.dll scrnblnk.txt scrnblnk.sys readme

clean:
         erase *.obj *.bak *.sts *.ilk *.sym *.sbr *.bsc *.lst *.map



