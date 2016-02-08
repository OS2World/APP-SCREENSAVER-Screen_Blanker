	     title	 ScrnDrvr -- OS/2 device driver for Screen Blanker
        page    55,132
        .286

; Copyright (c) 1991 Ballard Consulting.
;
; ScrnDrvr.ASM
;   Trivial device driver used to let Screen Blanker know about activity 
;   in the DOS box.  
;
;   The driver hooks the mouse vector and the "system services" software
;   interrupt, so it sees all mouse and keyboard actions in DOS mode.
;   (the mouse hook actually sees all mouse interrupts).
;   It just keeps a count. The Read entry returns the current
;   value of this count and resets it to zero.  The Read entry is called
;   by Screen Blanker from its timer code (when DOS box is active) to 
;   determine whether any activity has occurred since the last timer. 
;   (Timers are used here rather than a semaphore to avoid a lot of 
;   potential mode switches.)
;

        extrn   DosWrite:far
cr      equ     0dh
lf      equ     0ah
stdout  equ     1

; devhlp functions
PhysToVirt   equ 15h
UnPhysToVirt equ 32h
SetROMVector equ 1ah
SetIRQ       equ 1bh          

; bios system services
KeyboardIntercept equ 4fh

        page
DGROUP  group   _DATA

_DATA   segment word public 'DATA'

; Device driver header...
header  dd      -1              ; link to next device driver
        dw      8080h           ; character, 
                                ; no opens, level 1=os/2 
        dw      Strat           ; Strategy entry point
        dw	    0	              ; IDC entry point
        db      'SCRNBL$$'      ; logical device name
        db	    8 dup (0)       ; reserved

devhlp  dd      ?               ; DevHlp entry point
evcount dw      0               ; counter for key/mouse events
SavePrev dd     ?   

; Strategy routine dispatch table
dispch  dw      Init            ; 0  = initialize driver
        dw      Error           ; 1  = media check
        dw      Error           ; 2  = build BIOS parameter block
        dw      Error           ; 3  = not used
        dw      Read            ; 4  = read from device
;       dw  	 Error		     ; 5  = nondestructive read
;       dw      Error           ; 6  = return input status
;       dw      Error           ; 7  = flush device input buffers
;       dw      Write           ; 8  = write to device
maxcmd = ($-dispch)/2 - 1

ident   db      cr,lf
        db      'Screen Blanker 1.3b DOS Interface Driver installed'
        db      cr,lf
ident_len equ $-ident

noirqm  db      cr,lf
        db      'Screen Blanker driver can''t set mouse intercept'
        db      cr,lf
noirqm_len equ $-noirqm 

noromm  db      cr,lf,'Screen Blanker driver can''t set keyboard intercept'
        db      cr,lf
noromm_len equ $-noromm

wlen    dw      ?               ; return len from DosWrite
 
_DATA   ends

        page

_TEXT   segment word public 'CODE'

        assume  cs:_TEXT,ds:DGROUP,es:NOTHING

Strat   proc    far             ; Strategy entry point
                                ; ES:BX = request packet address

        mov     di,es:[bx+2]    ; get command code from packet
        and     di,0ffh
        cmp     di,maxcmd       ; supported by this driver?
        jle     Strat1          ; jump if command code OK

        call    Error           ; bad command code
        jmp     Strat2

Strat1: add     di,di           ; branch to command code routine
        call    word ptr [di+dispch]

Strat2: mov     es:[bx+3],ax    ; status into request packet
        ret                     ; back to OS/2 kernel

Strat   endp

;
; Mouse interrupt.  This just increments the event count
; then set the carry bit to indicate we didn't process this so it should
; be passed on.  This only works for non-AT machines (with edge triggered
; interrupts).
;
Intr    proc    far
        inc     evcount         ; count this
        stc                     ; "not mine"
        ret
Intr    endp

;
; System Services BIOS interrupt.  Checks for the keyboard intercept
; code and increments the event count.  In all cases, passes call
; on to previous interrupt routine.  
;
SSIntr  proc    far
        push    ds              ;save/load required DS 
        mov     ds,cs:SaveDS
        cmp     ah,KeyboardIntercept  ; is this what we want?n
        jne     NotKey          ;... no just continue
        inc     evcount         ;... yes, increment counter
NotKey:
; pass it on by faking an interrupt.  Can't just JMP, because we need
; to restore DS after.  
        pushf                   ; save flags for fake int 
        call    [SavePrev]      ; go to previous
        pop     ds              ; restore DS.
        iret

SaveDS  dw      ?               ; SetROMVector saves DS here. 
SSIntr    endp


; Command code routines are called by the Strategy routine
; via the Dispatch table with ES:BX pointing to the request
; header.  Each routine should return ES:BX unchanged
; and AX = status to be placed in request packet:
; 0100H if 'done' and no error
; 0000H if thread should block pending interrupt
; 81xxH if 'done' and error detected (xx=error code)

Read    proc    near            ; function 4 = read
        push    es              ; save request packet addr
        push    bx              ; ...

; Make sure we were given a big enough buffer:
        mov     cx,type evcount ; size required
        cmp     cx,es:[bx+12h]  ; vs size of buffer provided
        ja      BuffOops        ; not enough
     
        mov     es:[bx+12h],cx  ; set return size
        mov     ax,es:[bx+16]   ; high part of buffer phys addr
        mov     bx,es:[bx+14]   ; low part of buffer phys address
        mov     dh,1            ; result to es:di
        mov     dl,PhysToVirt   ; dev help function
        call    [devhlp]        ; get mapped
        jc      Oops            ; couldn't do it
        jz      MCOops          ; mode change shouldn't occur

        mov     ax,evcount      ; return current value
        mov     es:[di],ax ; ...
        mov     evcount,0       ; reset

        mov     dl,UnPhysToVirt ; unmap
        call    [devhlp]
        mov     ax,0100h        ; return 'done' status
        
        jmp     ReadRet 

BuffOops: mov  ax,81fdh         ; error buffer too small
        jmp     ReadRet

Oops:   mov     ax,81ffh        ; error return from phys to virt
        jmp     ReadRet

MCOops: mov     dl,UnPhysToVirt ; unmap
        call    [devhlp]
        mov     ax,81feh        ; error: mode change occurred  
           
ReadRet:
        pop     bx              ; restore
        pop     es              ; ...
        ret

Read    endp

Error   proc    near            ; bad command code
        mov     ax,8103h        ; error bit + 'done' status
                                ; + "Unknown Command" code
        ret
Error   endp

Init    proc    near            ; function 0 = initialize

        mov     ax,es:[bx+14]   ; get DevHlp entry point
        mov     word ptr devhlp,ax
        mov     ax,es:[bx+16]
        mov     word ptr devhlp+2,ax

; set offsets to end of code and data segments
        mov     word ptr es:[bx+14],offset _TEXT:Init
        mov     word ptr es:[bx+16],offset DGROUP:ident

; hook mouse IRQ vector
        push    bx
        mov     bx,12           ; want IRQ 12 
        mov     dh,1            ; interrupt sharing OK    
        mov     dl,SetIRQ       ; devhlp operation 
        mov     ax,offset _TEXT:Intr ;offset of ISR
        call    [devhlp]
        pop     bx
        jnc     IRQSet          ; test for failure to set it 
; couldn't set the mouse intercept
        mov     ax,offset DGROUP:noirqm
        mov     dx,noirqm_len   ; length of message
        call    Message         ; write it
; hook system services vector, to look for keyboard events
IRQSet: push    bx
        mov     ax,offset _TEXT:SSIntr  ; offset of ISR
        mov     bx,15h          ; int 15h is system services
        mov     si,offset _TEXT:SaveDS  ; place in code seg for ds
        mov     dl,SetROMVector ; devhlp operation 
        call    [devhlp]
        pop     bx
        jc      NoROM           ; failed to set? 
        mov     word ptr DGROUP:SavePrev,dx     ; save old value
        mov     word ptr DGROUP:SavePrev+2,ax   ; ...

; display sign-on message...
        mov     ax,offset DGROUP:ident
        mov     dx,ident_len    ; length of message
        call    Message         ; write it
                                      
        mov     ax,0100h        ; return 'done' status
        ret

; display fail messages...
NoROM:  mov     ax,offset DGROUP:noromm
        mov     dx,noromm_len   ; length of message
        call    Message         ; write it
        jmp     FailRet

FailRet: 
        mov     word ptr es:[bx+14],0    ; install failed
        mov     word ptr es:[bx+16],0         
        mov     ax,810ch        ; return 'fail' status
        ret

Init    endp

; Write a simple message -- valid from Init only.  Offset of message
; from dgroup should be in ax and length in bx. 
Message proc    near 
        push    stdout          ; standard output handle
        push    ds              ; address of message
        push    ax
        push    dx              ; length of message
        push    ds              ; receives bytes written
        push    offset DGROUP:wlen
        call    DosWrite        ; transfer to OS/2
        ret
Message endp
_TEXT   ends
        end
