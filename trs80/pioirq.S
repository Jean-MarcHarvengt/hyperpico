// *****************************
// PIO isr
// *****************************
	.syntax unified
	.section .time_critical, "ax"
	.cpu cortex-m0plus
	.thumb

.extern memory
.extern keyb_buffer

//Read pin
//ldr     r0, =SIO_BASE // 0xd0000000 + #define SIO_GPIO_IN_OFFSET _u(0x00000004)
//static inline bool gpio_get(uint gpio) {
//#if NUM_BANK0_GPIOS <= 32
//    return sio_hw->gpio_in & (1u << gpio);
//#else
//    if (gpio < 32) {
//        return sio_hw->gpio_in & (1u << gpio);
//    } else {
//        return sio_hw->gpio_hi_in & (1u << (gpio - 32));
//    }
//#endif
//}

// read PIO RX/TX
//PIO0_BASE _u(0x50200000)
//PIO1_BASE _u(0x50300000)
//PIO_RXF0_OFFSET _u(0x00000020)
//PIO_RXF1_OFFSET _u(0x00000024)
//PIO_RXF2_OFFSET _u(0x00000028)
//#define PIO_TXF0_OFFSET _u(0x00000010)
//#define PIO_TXF1_OFFSET _u(0x00000014)
//#define PIO_TXF2_OFFSET _u(0x00000018)

.thumb_func
.global getkeyb
getkeyb:
	push    {r2}
	movs    r2,r0
	ldr     r1, =keyb_buffer
	ldrb    r0, [r1,#0]
	asrs    r2,#1
	bcs     4f
	ldrb    r0, [r1,#1]
	lsrs    r2,#1
	bcs     4f
	ldrb    r0, [r1,#2]
	lsrs    r2,#1
	bcs     4f
	ldrb    r0, [r1,#3]
	lsrs    r2,#1
	bcs     4f
	ldrb    r0, [r1,#4]
	lsrs    r2,#1
	bcs     4f
	ldrb    r0, [r1,#5]
	lsrs    r2,#1
	bcs     4f
	ldrb    r0, [r1,#6]
	lsrs    r2,#1
	bcs     4f 
	ldrb    r0, [r1,#7]
	lsrs    r2,#1
	bcs     4f
	movs    r0, #0
4:
	pop    {r2}	
	mov   pc, lr



.thumb_func
.global pioirq_asmr
pioirq_asmr:
	push    {lr}
	push    {r0-r2}
	//if(!pio_sm_is_rx_fifo_empty(bus_pio, bus_smr)) {
	ldr     r0, =0x50200004 // PIO0_BASE +  PIO_FSTAT_OFFSET
	ldr     r2, [r0]
	movs    r1,#0x01		// mask for bit PIO_INTR_SM0_RXNEMPTY_LSB=0 + bus_smr=0
	tst     r2, r1 
	bne 	1f
	//uint16_t add = pio_sm_get(bus_pio, bus_smr); 
	//ldr     r0, =0x50200020 // PIO0_BASE +  PIO_RXF0_OFFSET
	//ldr     r1, [r0]
	ldr     r2, [r0,#0x1c] // address

 	ldr     r0, =0xd0000004  // SIO_BASE + SIO_GPIO_IN_OFFSET _u(0x00000004)
	ldr     r0, [r0]
	movs    r1, #1
	lsls    r1, r1, #27
	tst     r0, r1 
	bne 	0f
	movs    r1, #0xff
	b       3f
0:
   
	ldr     r0, =0x3800
    cmp     r2,r0
    blt     2f
	ldr     r0, =0x3900
    cmp     r2,r0
    bge     2f

	ldr     r0, =keyb_buffer
	ldrb    r1, [r0,#0]
	asrs    r2,#1
	bcs     3f
	ldrb    r1, [r0,#1]
	lsrs    r2,#1
	bcs     3f
	ldrb    r1, [r0,#2]
	lsrs    r2,#1
	bcs     3f
	ldrb    r1, [r0,#3]
	lsrs    r2,#1
	bcs     3f
	ldrb    r1, [r0,#4]
	lsrs    r2,#1
	bcs     3f
	ldrb    r1, [r0,#5]
	lsrs    r2,#1
	bcs     3f
	ldrb    r1, [r0,#6]
	lsrs    r2,#1
	bcs     3f 
	ldrb    r1, [r0,#7]
	lsrs    r2,#1
	bcs     3f
	movs    r1, #0
	b       3f

2:
	ldr     r0, =memory
	ldrb    r1, [r0,r2]

3:
	movs    r2, #128
	lsls    r2,#1
	orrs    r1,r2
	ldr     r0, =0x50200010 // PIO0_BASE +  PIO_TXF0_OFFSET
	str     r1, [r0]
1:
	pop     {r0-r2}
	pop     {pc}



.thumb_func
.global pioirq_asmw
pioirq_asmw:
	push    {lr}
	push    {r0-r2}
	//if(!pio_sm_is_rx_fifo_empty(bus_pio, bus_smw)) {
	ldr     r0, =0x50200004 // PIO0_BASE +  PIO_FSTAT_OFFSET
	ldr     r2, [r0]
	movs    r1,#0x02		// mask for bit PIO_INTR_SM0_RXNEMPTY_LSB=0 + bus_smw=1
	tst     r2, r1 
	bne 	20f
	//uint32_t value = pio_sm_get(bus_pio, bus_smw); 
	//ldr     r0, =0x50200024 // PIO0_BASE +  PIO_RXF1_OFFSET
	//ldr     r0, [r0]
	ldr     r0, [r0,#0x20]
	movs    r2,r0
	lsrs    r2,r2,8
	ldr     r1, =0xe000
    cmp     r2,r1	
    beq     20f
	ldr     r1, =0x3800
    cmp     r2,r1
    ble     20f
	movs    r2,r0
	lsrs    r0, r0, #8
	ldr     r1, =memory
	strb    r2,[r1,r0]
20:
	pop     {r0-r2}
	pop     {pc}
