-----------------------------------------------------------
Screen Blanker 1.3
Released as Open Source under the GNU GPL V2 License
2016/02/08
-----------------------------------------------------------

Screen Blanker 1.3b.  Copyright (c) Alan Ballard 1991.
 
See SCRNBLNK.TXT for installation and usage instructions. 

This version contains additional debugging code.  
If started with parameter /debugfile it will write a log file to
c:\scrnblnk.log  Do not use this normally, since the file will get
large and logging activity will adversly affect performance. 
 
The following changes were made in version 1.3:
 
1) Fixed a bug that caused it to sometimes fail to stay blanked in
   PM sessions. 
 
2) Added startup option /NOCLOSE that prevents disabling it once 
   started. 
 
3) Changed way non-PM OS/2 sessions are blanked so that if a program
   is running in the session when blanking occurs, it no longer gets
   suspended the first time it tries to write to the screen.  This is done 
   by first switching to the PM session. 
 
4) Added an optional ability to also blank while the DOS box is active.  
   Note that programs running in the DOS box *will* still be suspended
   as soon as blanking occurs. 
   Note that on some systems, this will not detect mouse activity
   in the DOS box.


