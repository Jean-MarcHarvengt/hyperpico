// *****************************
// Text 8x8 font blit functions
// background/forgreound
// *****************************
	.syntax unified
	.section .time_critical, "ax"
	.cpu cortex-m0plus
	.thumb

.macro bit2pix n
	asrs    r6,#1
	bcc		3f
	strb	r4,[r0,#\n]
	b 4f	
3:
	strb	r7,[r0,#\n]	
4:
.endm




// Registers:
//  R0 ... destination buffer
//  R1 ... source offset
//  R2 ... screen_width in chars
//  R3 ... LUT
//  Param1      ... font definition  [sp,#20]       
//  Param2      ... scroll [sp,#24]
//  Param3      ... char buffer [sp,#28]
//  Param4      ... col  buffer [sp,#32]

// reserved
//  R0 ... destination buffer
//  R1 ... source offset
//  R2 ... screen_width in chars
//  R3 ... LUT
//  R5 ... scroll shift 
// temporary
//  R7 ... tmp color bg
//  R4 ... tmp color fg
//  R6 ... (temporary)


.macro TextBlit n
	// push registers
	push	{r4-r7,lr}

	movs 	r5,#8
	ldr 	r6,[sp,#24]		// Param2: scroll	
	cmp     r6,#0
	beq 	6f				// no scroll

    // handle first char scroll
	subs	r5,r6			// 7-(scroll)

	ldr     r7,[sp,#28]     // charmap
	ldrb	r6,[r7,r1]		// char
	ldr     r7, [sp,#20] 	// + font start offset
	adds    r6,r7
	ldrb	r6,[r6]			// get char line bits
	ldr     r7,[sp,#32]     // colmap
	ldrb	r4,[r7,r1]		// col bg+fg
	lsls    r4,#1
	ldrb	r7,[r4,r3]      // clut bg 
	adds    r4,#1
	ldrb	r4,[r4,r3]      // clut fg

	rors    r6,r5
4:
	lsls	r6,#1    		// show first pixels
	bcc		5f
	strb	r3,[r0]	
5:	adds    r0,#1
	subs    r5,#1
	bne	 	4b
	bal     7f

    // handle middle chars scroll
6:
	ldr     r7,[sp,#28]     // charmap
	ldrb	r6,[r7,r1]		// char
	ldr     r7, [sp,#20] 	// + font start offset
	adds    r6,r7
	ldrb	r6,[r6]			// get char line bits
	ldr     r7,[sp,#32]     // colmap
	ldrb	r4,[r7,r1]		// col bg+fg
	lsls    r4,#1
	ldrb	r7,[r4,r3]      // clut bg 
	adds    r4,#1
	ldrb	r4,[r4,r3]      // clut fg
	bit2pix 7
	bit2pix 6
	bit2pix 5
	bit2pix 4
	bit2pix 3
	bit2pix 2
	bit2pix 1
	bit2pix 0	
	adds	r0,#8
7:
	adds    r1,#1
	cmp     r1,#\n
	bne   	8f
	movs    r1,#0  
8:
	subs	r2,#1
	bne	 	6b	

    // handle last char scroll
	ldr 	r5,[sp,#24]		// Param2: scroll	
	cmp     r5,#0
	beq 	11f				// no scroll

	ldr     r7,[sp,#28]     // charmap
	ldrb	r6,[r7,r1]		// char
	ldr     r7, [sp,#20] 	// + font start offset
	adds    r6,r7
	ldrb	r6,[r6]			// get char line bits
	ldr     r7,[sp,#32]     // colmap
	ldrb	r4,[r7,r1]		// col bg+fg
	lsls    r4,#1
	ldrb	r7,[r4,r3]      // clut bg 
	adds    r4,#1
	ldrb	r4,[r4,r3]      // clut fg

	movs    r1,#8
	rors    r6,r1
9:
	lsls	r6,#1    		// show first pixels
	bcc		10f
	strb	r3,[r0]	
10:	adds    r0,#1
	subs    r5,#1
	bne	 	9b
11:

	// pop registers and return from function
	pop	{r4-r7,pc}
.endm


.thumb_func
.global TextBlit32
TextBlit32:
	TextBlit 32

.thumb_func
.global TextBlit40
TextBlit40:
	TextBlit 40

.thumb_func
.global TextBlit64
TextBlit64:
	TextBlit 64

.thumb_func
.global TextBlit80
TextBlit80:
	TextBlit 80