
dw code_start

ORG   $4000   

code_start: 
LD   hl,scr   
LD   (hl),$48   
INC   hl   
LD   (hl),$45   
INC   hl   
LD   (hl),$4C   
INC   hl   
LD   (hl),$4C   
INC   hl   
LD   (hl),$4f   
INC   hl   
  
start:
ld de,scr
ld a, 0
copy:
ld   (de),a 
inc   de
inc   a
cp    0
jp nz,copy

JP   start




ORG   $3C00   
scr: 



