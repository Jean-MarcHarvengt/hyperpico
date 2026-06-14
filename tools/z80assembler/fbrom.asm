REG_TEXTMAP_L1:    equ $3C00
;
; data transfer
REG_TLOOKUP:       equ $fa00
; used as RGB332 LUT for pixels (palette) (WR)
; also used as 256 scratch buffer for other commands (WR/RD) 

REG_TCOMMAND:      equ $fb11
; WR
; 0: idle
; 1: transfer tiles data      (data=tilenr,w,h,packet pixels)
; 2: transfer sprites data    (data=spritenr,w,h,packet pixels)
; 3: transfer bitmap data     (data=xh,xl,y,wh,wl,h,w*h/packet pixels) 
; 4: transfer t/fmap col data (data=layer,col,row,size,size/packet tiles)
; 5: transfer t/fmap row data (data=layer,col,row,size,size/packet tiles)
; 6: transfer all tile 8bits data compressed (data=sizeh,sizel,pixels)
; 7: transfer all sprite 8bits data compressed (data=sizeh,sizel,pixels)
; 8: transfer bitmap 8bits data compressed (data=sizeh,sizel,pixels)
; 9: transfer font 1bit data, 8bits a time (data=sizeh,sizel,pixels)  

REG_TPARAMS:       equ $fb12
; WR

REG_TDATA:         equ $fb13
; WR

REG_TSTATUS:       equ $fb14
; transfer status (RD) 1=ready for async commands only

; bg/text color
REG_BG_COL:        equ $fb01
; RGB332
REG_FG_COL:        equ $fb0d

FILENAME_SIZE:    equ 20

CMD_OPENDIR:      equ 29
CMD_NEXTDIR:      equ 30
CMD_OPENFILE:     equ 27
CMD_READFILE:     equ 28


dw code_start

;ORG   $e701   
ORG   $eb00   

code_start: 
        jp topdir               ; goto root dir and clear

loop:   
        ld hl,path              ; copy path for opendir
        ld de,REG_TLOOKUP
copydir:
        ld a,(hl)
        ld (de),a
        inc hl
        inc de
        cp 0
        jr nz,copydir



        ld a,CMD_OPENDIR        ; cmd_opendir
        ld (REG_TCOMMAND),a        
        ld a,0
        ld (curpage), a
loopdir:        
        ld a,(REG_TSTATUS)      ; wait status done
        or a
        jr nz,loopdir

        ld de,REG_TLOOKUP
        ld a,(de)
        ld (nbfiles),a          ; nb of files
        cp 0
        jr z,emptydir

        ld hl,filenames
        ld b,0                  ; file index
nextfile:                       ; read filenames char by char
        
        inc de
        ld a ,(de)              ; filetype 0=DIR,1=PRG,2=ROM
        ;cp $ff
        ;jp z,emptydir

        push hl
        ld c,a
        ld hl, filetypes
        ld a,b
        add a,l                 ; +b on 16bits!
        ld l,a
        adc a,h
        sub l
        ld h,a
        ld a,c                  ; store filetype
        ld (hl),a
        pop hl
        push hl

nextchar:
        inc de
        ld a ,(de)              ; charloop
        cp 0
        jp   z,endfilename
        ld (hl),a
        inc hl
        jr nextchar 

endfilename:
        pop hl
        ld a,FILENAME_SIZE                 
        add a,l                 ; +FILENAME_SIZE on 16bits!
        ld l,a
        adc a,h
        sub l
        ld h,a

        inc b                   ; next file
        ld a, (nbfiles) 
        cp b
        jr   nz,nextfile

emptydir:
        call print_file_menu 
waitkey:        
        call getkey
        ld b,'W'                ; page? 
        cp b
        jp z,nextpage
        ld b,'Q'                ; topdir? 
        cp b
        jp z,topdir
        ld b,$0d                ; return? 
        cp b
        jp z,exit

        ld b,$30                ; < $30
        cp b
        jr c, waitkey            ; less
        ld b,$3A                ; < $3a
        cp b
        jr c, got_number        ; less
        jr waitkey

got_number:    
        sub a,$30

        ld b,a
        ld a,(nbfiles)
        cp b
        jr c, waitkey           ; less 
        jr z, waitkey

        
        ld hl, filetypes
        ld a,b
        add a,l                 ; +b on 16bits!
        ld l,a
        adc a,h
        sub l
        ld h,a
        ld a,(hl)
        cp 0
        jr   z,readdir


;        ld a,(loader_size)
;        ld c,a
;        ld hl,loader_start      ; loader
;        ld de,$3c00
;copyloader:
;        ld a,(hl)
;        ld (de),a
;        inc hl
;        inc de
;        dec c
;        ld a,c
;        cp 0
;        jr nz,copyloader
;        jp $3c00

        jp loadfile

readdir:
        call append_to_path     ; b contains file index
        call clear_files        
        call clear_screen
        jp loop









nextpage:
        call clear_files        
        ld a,CMD_NEXTDIR        ; cmd_nextdir
        ld (REG_TCOMMAND),a   
        ld a,(curpage),a
        add 1
        ld (curpage),a
        jp loopdir


topdir:
        ld a,0                  ; path = "" 
        ld (path),a
        ld (dirpt),a
        call clear_files        
        call clear_screen
        jp loop
        



start:
;ld a, $80
;ld (REG_BG_COL), a

        call clear_screen
        ld   hl,header
        ld   de,REG_TEXTMAP_L1+64*10
        call print

        call getkey
        ld (REG_TEXTMAP_L1+64*10),a

exit:
;ld a, $80
;ld (REG_BG_COL), a
        ret

; #######################################@
; wait for command execution
; #######################################@
;waitstatusdone:
;        ld a,(REG_TSTATUS)
;        or a
;        jr nz,waitstatusdone
;        ret



; #######################################@
; extend path with filename at index
; b = index
; #######################################@
append_to_path:      
        ld a,b                  ; fileindex *20
        add a,a                 
        add a,a
        ld b,a
        add a,a
        add a,a
        add a,b
        ld de,filenames         ; + filenames
        add a,e
        ld e,a
        adc a,d
        sub e
        ld d,a
        ld a,(dirpt)             ; get dirpt
        ld b,a
        ld hl,path
        add a,l                 ; +dirpt on 16bits!
        ld l,a
        adc a,h
        sub l
        ld h,a
        ld a,'/'
        ld (hl),a
        inc hl
        inc b
extendpath:
        ld a,(de)
        ld (hl),a
        inc hl
        inc de
        inc b
        cp 0
        jr nz,extendpath
        ld a,b
        dec a
        ld (dirpt),a
        ret

; #######################################@
; wait a key (key char in A register)
; #######################################@
getkey:        
        CALL 002BH
        or a
        jr   z,getkey
        ret


; #######################################@
; print text at FB location
;
; HL: text pointer (end with 0)
; DE: framebuffer address e.g. REG_TEXTMAP_L1+64*10 
;
; #######################################@
print:
        ld   a,(hl)
        cp   0
        jr   z,print_exit
        ld   (de),a
        inc  hl
        inc  de
        jr   print
print_exit:
        ret        


; #######################################@
; clear the full screen
; #######################################@
clear_screen:
        ld   de,REG_TEXTMAP_L1
        ld   hl,REG_TEXTMAP_L1+16*64
        sbc  hl,de
clears1:        
        ld  a, 32
        ld  (de),a
        inc de
        dec hl
        ld a,h
        or l
        jr nz,clears1  
        ret

; #######################################@
; clear all filenames strings
; #######################################@
clear_files:
        ld de,filenames
        ld hl,filenames_end
        sbc hl,de
clearf1:        
        ld a,0
        ld (de),a
        inc de
        dec hl
        ld a,h
        or l
        jr nz,clearf1
        ret

; #######################################@
; print number before filenames
; #######################################@
print_numbers:                  
        ld   c,0
        ld   hl,REG_TEXTMAP_L1+2*64
        ld   de,numbers
pri_num0:
        ld a,(nbfiles)
        cp c
        jr z,pri_exit
        ld  a,(de)
        ld  (hl),a
        inc de                  ; next number
        ld a,64                 ; 64 char per line
        add a,l                 ; +64 on 16bits!
        ld l,a
        adc a,h
        sub l
        ld h,a
        inc c 
        jr pri_num0
pri_exit:                
        ret

; #######################################@
; print filebrowser menu
; #######################################@
print_file_menu:
        call clear_screen       ; clear screen

        ld   hl,header          ; header/title
        ld   de,REG_TEXTMAP_L1+64*0
        call print

        ld   hl,footer1         ; footer1
        ld   de,REG_TEXTMAP_L1+64*13
        call print

        ld   hl,footer2         ; footer2
        ld   de,REG_TEXTMAP_L1+64*14
        call print

        ld   hl,path
        ld   de,REG_TEXTMAP_L1+64*1
        call print
 
        call print_numbers      ; print numbers in from of filename

        ld   hl,line0           ; print 10 filenames list
        ld   de,REG_TEXTMAP_L1+64*2+2
        call print
        ld   hl,line1
        ld   de,REG_TEXTMAP_L1+64*3+2
        call print
        ld   hl,line2
        ld   de,REG_TEXTMAP_L1+64*4+2
        call print
        ld   hl,line3
        ld   de,REG_TEXTMAP_L1+64*5+2
        call print
        ld   hl,line4
        ld   de,REG_TEXTMAP_L1+64*6+2
        call print
        ld   hl,line5
        ld   de,REG_TEXTMAP_L1+64*7+2
        call print
        ld   hl,line6
        ld   de,REG_TEXTMAP_L1+64*8+2
        call print
        ld   hl,line7
        ld   de,REG_TEXTMAP_L1+64*9+2
        call print
        ld   hl,line8
        ld   de,REG_TEXTMAP_L1+64*10+2
        call print
        ld   hl,line9
        ld   de,REG_TEXTMAP_L1+64*11+2
        call print
        ret


; #######################################@
; All variables below
; #######################################@
curpage:
db 0
nbfiles:
db 0
numbers:
db "0123456789"
dirpt:
db 0
path:
db "0123456789abcdef0123456789abcdef"
db "0123456789abcdef0123456789abcdef"
db "0123456789abcdef0123456789abcdef"
db "0123456789abcdef0123456789abcdef"

header:
db "FILE BROWSER 1.0, SELECT FILE:"
db 0
footer1:
db "W=PAGE RET=EXIT R=RESET"
db 0
footer2:
db "(0-9) LOAD/OPENDIR Q=TOPDIR"
db 0

filetypes:
db 0,0,0,0,0,0,0,0,0,0

filenames:
line0:
db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
line1:
db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
line2:
db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
line3:
db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
line4:
db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
line5:
db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
line6:
db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
line7:
db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
line8:
db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
line9:
db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
filenames_end:

loader_size:
db (loader_end-loader_start)
db 0

loader_start:
;incbin 'loader.bin'
include 'loader.asm'
loader_end:

