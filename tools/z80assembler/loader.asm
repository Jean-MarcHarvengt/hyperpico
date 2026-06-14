XREG_TEXTMAP_L1:    equ $3C00
;
; data transfer
XREG_TLOOKUP:       equ $fa00
; used as RGB332 LUT for pixels (palette) (WR)
; also used as 256 scratch buffer for other commands (WR/RD) 

XREG_TCOMMAND:      equ $fb11
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

XREG_TPARAMS:       equ $fb12
; WR

XREG_TDATA:         equ $fb13
; WR

XREG_TSTATUS:       equ $fb14
; transfer status (RD) 1=ready for async commands only

; bg/text color
XREG_BG_COL:        equ $fb01


XCMD_OPENFILE:     equ 27
XCMD_READFILE:     equ 28



;ORG   $3c00 

; #######################################@
; load CMD file and execute
; b = file index
; #######################################@
loadfile:
        ld a,XCMD_OPENFILE       ; cmd_openfile
        ld (XREG_TCOMMAND),a
        ld a,b                  ; b contains file index
        ld (XREG_TPARAMS),a
waitstatus1:
        ld a,(XREG_TSTATUS)
        or a
        jr nz,waitstatus1
        ld a,(XREG_TLOOKUP)      ; check if file could be opened
        cp 0
        jp z,error_loadfile
nextblock:
        ld b,1                  ; load cmd record block type
        call readfile_nbytes
        cp 1
        jp nz,error_loadfile
        ld a,(XREG_TLOOKUP+1)
        cp 1                    ; type <LOAD> block?
        jr nz,next_block_type1

        ld b,1                  ; load len
        call readfile_nbytes
        cp 1
        jp nz,error_loadfile
        ld a,(XREG_TLOOKUP+1)
        ld c,a                  ; len
        ld b,2                  ; load address
        call readfile_nbytes
        cp 2
        jp nz,error_loadfile
        ld a,(XREG_TLOOKUP+1)    ; lo
        ld l,a
        ld a,(XREG_TLOOKUP+2)    ; hi
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
        jr load
nextl2:
        cp 2                    ; 2+256-2 => 256
        jr nz,nextl3
        ld c,1

        ld b,c                  ; load len
        call readfile_nbytes
        cp c
        jp nz,error_loadfile
        ld de,XREG_TLOOKUP+1
        ld b,c
        call copy
        ld c,255
        jr load
nextl3:
        ld a,c
        sub a,2
        ld c,a
load:
        ld b,c                  ; load len
        call readfile_nbytes
        cp c
        jp nz,error_loadfile
        ld de,XREG_TLOOKUP+1
        ld b,c
        call copy

        jr nextblock


next_block_type1:
        cp 2                    ; type <ENTRY_ADDRESS> block?
        jp nz,next_block_type2
        ld b,1                  ; load len
        call readfile_nbytes
        cp 1
        jp nz,error_loadfile
        ld a,(XREG_TLOOKUP+1)
        ld b,a
        call readfile_nbytes
        ld a,(XREG_TLOOKUP+1)    ; lo
        ld (startaddress+1),a
        ld a,(XREG_TLOOKUP+2)    ; hi
        ld (startaddress+2),a
        ld a, $00
        ld (XREG_BG_COL), a
startaddress:        
        jp startaddress


next_block_type2:
        cp 5                    ; type <HEADER> block?
        jr nz,next_block_type3
        ld b,1                  ; load len
        call readfile_nbytes
        cp 1
        jp nz,error_loadfile
        ld a,(XREG_TLOOKUP+1)
        ld b,a
        call readfile_nbytes
        jp nextblock


next_block_type3:
        ret

; #######################################@
; read n bytes from filept
; b = nb bytes to read (max 255)
; #######################################@
readfile_nbytes:
        ld a,XCMD_READFILE       ; cmd_readfile
        ld (XREG_TCOMMAND),a
        ld a,b                  ; b contains nb bytes
        ld (XREG_TPARAMS),a
waitstatus:
        ld a,(XREG_TSTATUS)
        or a
        jr nz,waitstatus
        ld a,(XREG_TLOOKUP)      ; a = nb bytes read
        ret

error_loadfile:
        add a,$30
        ld (XREG_TEXTMAP_L1),a
        ld a, $80
        ld (XREG_BG_COL), a
error:
        jr error

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
        ld a,(XREG_BG_COL)
        inc a
        ld (XREG_BG_COL),a
        ret

