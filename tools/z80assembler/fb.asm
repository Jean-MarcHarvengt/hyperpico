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

;ORG   $e001   
ORG   $d800   

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
        jp nz,copydir



        ld a,CMD_OPENDIR        ; cmd_opendir
        ld (REG_TCOMMAND),a        
        ld a,0
        ld (curpage), a
loopdir:        
        call waitstatusdone

        ld de,REG_TLOOKUP
        ld a,(de)
        ld (nbfiles),a          ; nb of files
        cp 0
        jp z,emptydir

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
        jp nextchar 

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
        jp   nz,nextfile

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
        jp waitkey

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
        jp   nz,loadfile

readdir:
        call append_to_path     ; b contains file index
        call clear_files        
        call clear_screen
        jp loop

loadfile:
        ld a,CMD_OPENFILE       ; cmd_openfile
        ld (REG_TCOMMAND),a
        ld a,b                  ; b contains file index
        ld (REG_TPARAMS),a
        call waitstatusdone
        ld a,(REG_TLOOKUP)      ; check if file could be opened
        cp 0
        jp z,error_loadfile
nextblock:
        ld b,1                  ; load cmd record block type
        call readfile_nbytes
        cp 1
        jp nz,error_loadfile
        ld a,(REG_TLOOKUP+1)
        cp 1                    ; type <LOAD> block?
        jp nz,next_block_type1

        ld b,1                  ; load len
        call readfile_nbytes
        cp 1
        jp nz,error_loadfile
        ld a,(REG_TLOOKUP+1)
        ld c,a                  ; len
        ld b,2                  ; load address
        call readfile_nbytes
        cp 2
        jp nz,error_loadfile
        ld a,(REG_TLOOKUP+1)    ; lo
        ld l,a
        ld a,(REG_TLOOKUP+2)    ; hi
        ld h,a
        ld a,c
        cp 0                    ; 0+256-2 => 254
        jr nz,nextl1
        ld c,254
        jp load
nextl1:
        cp 1                    ; 1+256-2 => 255
        jr nz,nextl2
        ld c,255
        jp load
nextl2:
        cp 2                    ; 2+256-2 => 256
        jr nz,nextl3
        ld c,1

        ld b,c                  ; load len
        call readfile_nbytes
        cp c
        jp nz,error_loadfile
        ld de,REG_TLOOKUP+1
        ld b,c
        call copy
        ld c,255
        jp load
nextl3:
        ld a,c
        sub a,2
        ld c,a
load:
        ld b,c                  ; load len
        call readfile_nbytes
        cp c
        jp nz,error_loadfile
        ld de,REG_TLOOKUP+1
        ld b,c
        call copy

        jp nextblock


next_block_type1:
        cp 2                    ; type <ENTRY_ADDRESS> block?
        jp nz,next_block_type2
        ld b,1                  ; load len
        call readfile_nbytes
        cp 1
        jp nz,error_loadfile
        ld a,(REG_TLOOKUP+1)
        ld b,a
        call readfile_nbytes
        ld a,(REG_TLOOKUP+1)    ; lo
        ld (startaddress+1),a
        ld a,(REG_TLOOKUP+2)    ; hi
        ld (startaddress+2),a
;        ld a,(REG_TLOOKUP+1)
;        rra
;        rra
;        rra
;        rra
;        add $30
;        ld (REG_TEXTMAP_L1),a
;        ld a,(REG_TLOOKUP+1)
;        and $f
;        add $30
;        ld (REG_TEXTMAP_L1+1),a
;        ld a,(REG_TLOOKUP+2)
;        rra
;        rra
;        rra
;        rra
;        add $30
;        ld (REG_TEXTMAP_L1+2),a
;        ld a,(REG_TLOOKUP+2)
;        and $f
;        add $30
;        ld (REG_TEXTMAP_L1+3),a      
        ld a, $00
        ld (REG_BG_COL), a
startaddress:        
        jp startaddress


next_block_type2:
        cp 5                    ; type <HEADER> block?
        jp nz,next_block_type3
        ld b,1                  ; load len
        call readfile_nbytes
        cp 1
        jp nz,error_loadfile
        ld a,(REG_TLOOKUP+1)
        ld b,a
        call readfile_nbytes
        jp nextblock


next_block_type3:
        jp exit

error_loadfile:
        add a,$30
        ld (REG_TEXTMAP_L1),a
        ld a, $80
        ld (REG_BG_COL), a
        jp waitkey




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
waitstatusdone:
        ld a,(REG_TSTATUS)
        or a
        jr nz,waitstatusdone
        ret

; #######################################@
; read n bytes from filept
; b = nb bytes to read (max 255)
; #######################################@
readfile_nbytes:
        ld a,CMD_READFILE       ; cmd_readfile
        ld (REG_TCOMMAND),a
        ld a,b                  ; b contains nb bytes
        ld (REG_TPARAMS),a
        call waitstatusdone
        ld a,(REG_TLOOKUP)      ; a = nb bytes read
        ret

; #######################################@
; copy block
; de = source
; hl = destination
; b = len
; #######################################@
copy:
        ld a,(de)
        ld (hl),a
        inc de
        inc hl
        dec b
        ld a,b
        cp 0
        jr nz,copy
        ld a,(REG_BG_COL)
        inc a
        ld (REG_BG_COL),a
        ret

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
