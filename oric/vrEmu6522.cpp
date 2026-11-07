/*
 * Troy's 6522 Emulator
 *
 * Copyright (c) 2023 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/vrEmu6522
 *
 */

#include "vrEmu6522.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "pico/stdlib.h"


 /* ------------------------------------------------------------------
  * PRIVATE DATA STRUCTURE
  */
struct vrEmu6522_s
{
  vrEmu6522Model model;

  uint8_t reg[16];

  bool t1Active;
  bool t2Active;

  bool intPin;

  // KEYBOARD
  uint8_t kCol;
  uint8_t kM[8];
  // JOYSTICK
  uint8_t joy;
  uint8_t joysel; // bit 7=0 =>left, bit 6=0 =>right,

  uint8_t IRB;
  uint8_t IRA;
};

/* ------------------------------------------------------------------
 * PRIVATE CONSTANTS
 */

#define VIA_REG_PORT_B  0x00
#define VIA_REG_PORT_A  0x01
#define VIA_REG_DDR_B   0x02
#define VIA_REG_DDR_A   0x03
#define VIA_REG_T1C_L   0x04
#define VIA_REG_T1C_H   0x05
#define VIA_REG_T1L_L   0x06
#define VIA_REG_T1L_H   0x07
#define VIA_REG_T2C_L   0x08
#define VIA_REG_T2C_H   0x09
#define VIA_REG_SR      0x0a
#define VIA_REG_ACR     0x0b
#define VIA_REG_PCR     0x0c
#define VIA_REG_IFR     0x0d
#define VIA_REG_IER     0x0e
#define VIA_REG_PORT_Ax 0x0f


#define VIA_IFR_CA2 (1 << 0)
#define VIA_IFR_CA1 (1 << 1)
#define VIA_IFR_SR  (1 << 2)
#define VIA_IFR_CB2 (1 << 3)
#define VIA_IFR_CB1 (1 << 4)
#define VIA_IFR_T2  (1 << 5)
#define VIA_IFR_T1  (1 << 6)
#define VIA_IFR_IRQ (1 << 7)

 /* ------------------------------------------------------------------
  *  HELPER FUNCTIONS
  * ----------------------------------------------------------------*/
inline static void viaIfrSetIrq(VrEmu6522* vr6522)
{
  uint8_t ier = vr6522->reg[VIA_REG_IER] & 0x7f;
  uint8_t ifr = vr6522->reg[VIA_REG_IFR] & 0x7f;

  if (ier & ifr) ifr |= 0x80;

  vr6522->reg[VIA_REG_IFR] = ifr;
  vr6522->intPin = (vr6522->reg[VIA_REG_IFR] & 0x80) ? true : false;
}

inline static void viaIfrSetBit(VrEmu6522* vr6522, uint8_t bit)
{
  vr6522->reg[VIA_REG_IFR] |= bit & 0x7f;
  viaIfrSetIrq(vr6522);
}

inline static void viaIfrResetBit(VrEmu6522* vr6522, uint8_t bit)
{
  vr6522->reg[VIA_REG_IFR] &= ~(bit & 0x7f);
  viaIfrSetIrq(vr6522);
}

inline static void viaIerSet(VrEmu6522* vr6522, uint8_t bit)
{
  if (bit & 0x80)
  {
    vr6522->reg[VIA_REG_IER] |= 0x80 | bit;
  }
  else
  {
    vr6522->reg[VIA_REG_IER] &= 0x80 | ~bit;
  }
}

inline static void viaPsgBus(VrEmu6522* vr6522) {
      vr6522->kCol = (~(vr6522->reg[VIA_REG_PORT_A] & vr6522->reg[VIA_REG_DDR_A])) & 0xFF;
}


/* ------------------------------------------------------------------
 *
 * create a new 6522
 */
VrEmu6522* vrEmu6522New(
  vrEmu6522Model model)
{
  VrEmu6522* vr6522 = (VrEmu6522*)malloc(sizeof(VrEmu6522));
  if (vr6522 != NULL)
  {
    vr6522->model = model;

    vrEmu6522Reset(vr6522);
  }

  return vr6522;
}

/*
 * destroy a 6522
 */
VR_EMU_6522_DLLEXPORT void vrEmu6522Destroy(VrEmu6522* vr6522)
{
  if (vr6522)
  {
    /* destruction */
    free(vr6522);
  }
}

/*
 * reset the 6522
 */
VR_EMU_6522_DLLEXPORT void vrEmu6522Reset(VrEmu6522* vr6522)
{
  if (vr6522)
  {
    for (int i = 0; i < 4; ++i)
    {
      vr6522->reg[i] = 0;
    }

    for (int i = 11; i < 16; ++i)
    {
      vr6522->reg[i] = 0;
    }

    vr6522->t1Active = false;
    vr6522->t2Active = false;

    /* initialization */
    vr6522->intPin = false;

    for (int i = 0; i < 8; ++i)
    {
      vr6522->kM[i] = 0;
    }  
    vr6522->kCol = 0;
  }
}

/*
 * write to the via
 */


//static uint8_t hex[16]={48,49,50,51,52,53,54,55,56,57,65,66,67,68,69,70};
//extern uint8_t memory[];


static void __not_in_flash("write0") write0(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
//  VIA_REG_PORT_B
      viaIfrResetBit(vr6522, 0x18);
      vr6522->reg[addr] = data;
}

static void __not_in_flash("write1") write1(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
//VIA_REG_PORT_A  
      viaIfrResetBit(vr6522, 0x03);
      viaPsgBus(vr6522);
      vr6522->reg[addr] = data;
      vr6522->joysel = data;
}

static void __not_in_flash("write2") write2(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
//VIA_REG_DDR_B
      vr6522->reg[addr] = data;
}

static void __not_in_flash("write3") write3(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
//VIA_REG_DDR_A
      viaPsgBus(vr6522);
      vr6522->reg[addr] = data;
      //if (data != 0xff) {
      //  memory[0xBB86] = hex[(data>>4) & 0xf];
      //  memory[0xBB86+40] = hex[(data>>4) & 0xf];
      //  memory[0xBB87] = hex[(data) & 0xf];
      //  memory[0xBB87+40] = hex[(data) & 0xf];        
      //}
}

static void __not_in_flash("write4") write4(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
//VIA_REG_T1C_L;
      uint8_t reg = VIA_REG_T1L_L;
      vr6522->reg[reg] = data;
}

static void __not_in_flash("write5") write5(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
//VIA_REG_T1C_H;
      uint8_t reg = VIA_REG_T1L_H;
      vr6522->reg[VIA_REG_T1C_H] = data;
      vr6522->reg[VIA_REG_T1C_L] = vr6522->reg[VIA_REG_T1L_L];
      viaIfrResetBit(vr6522, VIA_IFR_T1);
      vr6522->t1Active = true;
      vr6522->reg[reg] = data;
}

static void __not_in_flash("write6") write6(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
      vr6522->reg[addr] = data;
}

static void __not_in_flash("write7") write7(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
      vr6522->reg[addr] = data;
}

static void __not_in_flash("write8") write8(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
      vr6522->reg[addr] = data;
}

static void __not_in_flash("write9") write9(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
//VIA_REG_T2C_H
      viaIfrResetBit(vr6522, VIA_IFR_T2);
      vr6522->t2Active = true;
      vr6522->reg[addr] = data;
}

static void __not_in_flash("writea") writea(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
      vr6522->reg[addr] = data;
}

static void __not_in_flash("writeb") writeb(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
      vr6522->reg[addr] = data;
}

static void __not_in_flash("writec") writec(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
//VIA_REG_PCR
      viaPsgBus(vr6522);
      vr6522->reg[addr] = data;
}

static void __not_in_flash("writed") writed(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
//VIA_REG_IFR:
      viaIfrResetBit(vr6522, data);
      data = vr6522->reg[addr];
      vr6522->reg[addr] = data;
}

static void __not_in_flash("writee") writee(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
//VIA_REG_IER:
      viaIerSet(vr6522, data);
      data = vr6522->reg[addr];
      vr6522->reg[addr] = data;
}

static void __not_in_flash("writef") writef(VrEmu6522* vr6522, uint8_t addr, uint8_t data) {
//VIA_REG_PORT_Ax:
      vr6522->reg[VIA_REG_PORT_A] = data;
      vr6522->reg[addr] = data;
}

void __not_in_flash("iowriteFuncTable") (*iowriteFuncTable[16])(VrEmu6522* vr6522, uint8_t addr, uint8_t data)
{
  write0,
  write1,
  write2,
  write3,
  write4,
  write5,
  write6,
  write7,
  write8,
  write9,
  writea,
  writeb,
  writec,
  writed,
  writee,
  writef
};

/*
VR_EMU_6522_DLLEXPORT void __not_in_flash("vrEmu6522Write") vrEmu6522Write(VrEmu6522* vr6522, uint8_t addr, uint8_t data)
{
  uint8_t reg = addr & 0x0f;

  switch (reg)
  {
    case VIA_REG_PORT_B:
      viaIfrResetBit(vr6522, 0x18);
      break;
    case VIA_REG_PORT_A:
      viaIfrResetBit(vr6522, 0x03);
      viaPsgBus(vr6522);
      break;
    case VIA_REG_DDR_B:
      break;
    case VIA_REG_DDR_A:
      viaPsgBus(vr6522);
      break;

    case VIA_REG_T1C_L:
      reg = VIA_REG_T1L_L;
      break;

    case VIA_REG_T1C_H:
      reg = VIA_REG_T1L_H;
      vr6522->reg[VIA_REG_T1C_H] = data;
      vr6522->reg[VIA_REG_T1C_L] = vr6522->reg[VIA_REG_T1L_L];
      viaIfrResetBit(vr6522, VIA_IFR_T1);
      vr6522->t1Active = true;
      break;

    case VIA_REG_T2C_H:
      viaIfrResetBit(vr6522, VIA_IFR_T2);
      vr6522->t2Active = true;
      break;

    case VIA_REG_PCR:
      viaPsgBus(vr6522);
      break;

    case VIA_REG_IFR:
      viaIfrResetBit(vr6522, data);
      data = vr6522->reg[reg];
      break;

    case VIA_REG_IER:
      viaIerSet(vr6522, data);
      data = vr6522->reg[reg];
      break;

    case VIA_REG_PORT_Ax:
      vr6522->reg[VIA_REG_PORT_A] = data;
      break;
  }

  vr6522->reg[reg] = data;
}
*/

/*
 * read from the via
 */


static uint8_t __not_in_flash("read0") read0(VrEmu6522* vr6522, uint8_t addr) {
//  VIA_REG_PORT_B
      viaIfrResetBit(vr6522, 0x18); // reset CB2 CB1 interrupt flag                       
      if ((vr6522->reg[VIA_REG_DDR_B] & 0x08) == 0) {                      // PB3 as input
          vr6522->IRB &= ~0x08;                                    // PB3 reset
          vr6522->IRB |= ((vr6522->kM[vr6522->reg[VIA_REG_PORT_B] & 0x07] & vr6522->kCol)) ? 0x08 : 0x00;
      }
      return (vr6522->IRB & (~vr6522->reg[VIA_REG_DDR_B])) | (vr6522->reg[VIA_REG_PORT_B] & vr6522->reg[VIA_REG_DDR_B]);
}


static uint8_t __not_in_flash("read1") read1(VrEmu6522* vr6522, uint8_t addr) {
//VIA_REG_PORT_A  
      viaIfrResetBit(vr6522, 0x03);
      viaPsgBus(vr6522);
      //memory[0xBB84] = hex[(vr6522->joysel >>4) & 0xf];
      //memory[0xBB84+40] = hex[(vr6522->joysel >>4) & 0xf];
      //memory[0xBB85] = hex[(vr6522->joysel & 0xf)];
      //memory[0xBB85+40] = hex[(vr6522->joysel & 0xf)];
      if ( (vr6522->joysel & 0x80) == 0 ) {
        vr6522->IRA = vr6522->joy; 
      }
      else {
        vr6522->IRA = 0xdf;
      }
      return (vr6522->IRA & (~vr6522->reg[VIA_REG_DDR_A])) | (vr6522->reg[VIA_REG_PORT_A] & vr6522->reg[VIA_REG_DDR_A]);
}

static uint8_t __not_in_flash("read2") read2(VrEmu6522* vr6522, uint8_t addr) {
      return(vr6522->reg[addr & 0x0f]);
}

static uint8_t __not_in_flash("read3") read3(VrEmu6522* vr6522, uint8_t addr) {
      return(vr6522->reg[addr & 0x0f]);
}

static uint8_t __not_in_flash("read4") read4(VrEmu6522* vr6522, uint8_t addr) {
      viaIfrResetBit(vr6522, VIA_IFR_T1);
      return(vr6522->reg[addr & 0x0f]);
}

static uint8_t __not_in_flash("read5") read5(VrEmu6522* vr6522, uint8_t addr) {
      return(vr6522->reg[addr & 0x0f]);
}

static uint8_t __not_in_flash("read6") read6(VrEmu6522* vr6522, uint8_t addr) {
      return(vr6522->reg[addr & 0x0f]);
}

static uint8_t __not_in_flash("read7") read7(VrEmu6522* vr6522, uint8_t addr) {
      return(vr6522->reg[addr & 0x0f]);
}

static uint8_t __not_in_flash("read8") read8(VrEmu6522* vr6522, uint8_t addr) {
      vr6522->t2Active = false;
      viaIfrResetBit(vr6522, VIA_IFR_T2);
      return(vr6522->reg[addr & 0x0f]);
}

static uint8_t __not_in_flash("read9") read9(VrEmu6522* vr6522, uint8_t addr) {
      return(vr6522->reg[addr & 0x0f]);
}

static uint8_t __not_in_flash("reada") reada(VrEmu6522* vr6522, uint8_t addr) {
      return(vr6522->reg[addr & 0x0f]);
}

static uint8_t __not_in_flash("readb") readb(VrEmu6522* vr6522, uint8_t addr) {
      return(vr6522->reg[addr & 0x0f]);
}

static uint8_t __not_in_flash("readc") readc(VrEmu6522* vr6522, uint8_t addr) {
      return(vr6522->reg[addr & 0x0f]);
}

static uint8_t __not_in_flash("readd") readd(VrEmu6522* vr6522, uint8_t addr) {
      return(vr6522->reg[addr & 0x0f]);
}

static uint8_t __not_in_flash("reade") reade(VrEmu6522* vr6522, uint8_t addr) {
      return(vr6522->reg[addr & 0x0f]);
}

static uint8_t __not_in_flash("readf") readf(VrEmu6522* vr6522, uint8_t addr) {
      viaPsgBus(vr6522);
      return (vr6522->IRA & (~vr6522->reg[VIA_REG_DDR_A])) | (vr6522->reg[VIA_REG_PORT_Ax] & vr6522->reg[VIA_REG_DDR_A]);
}



uint8_t __not_in_flash("ioreadFuncTable") (*ioreadFuncTable[16])(VrEmu6522* vr6522, uint8_t addr)
{
  read0,
  read1,
  read2,
  read3,
  read4,
  read5,
  read6,
  read7,
  read8,
  read9,
  reada,
  readb,
  readc,
  readd,
  reade,
  readf
};


/*
uint8_t __not_in_flash("vrEmu6522Read") vrEmu6522Read(VrEmu6522* vr6522, uint8_t addr) {

  uint8_t reg = addr & 0x0f;
  uint8_t value = vr6522->reg[addr & 0x0f];

  switch (reg)
  {
    case VIA_REG_PORT_B:
      viaIfrResetBit(vr6522, 0x18); // reset CB2 CB1 interrupt flag                       
      if ((vr6522->reg[VIA_REG_DDR_B] & 0x08) == 0) {                      // PB3 as input
          vr6522->IRB &= ~0x08;                                    // PB3 reset
          vr6522->IRB |= ((vr6522->kM[vr6522->reg[VIA_REG_PORT_B] & 0x07] & vr6522->kCol)) ? 0x08 : 0x00;
      }
      return (vr6522->IRB & (~vr6522->reg[VIA_REG_DDR_B])) | (vr6522->reg[VIA_REG_PORT_B] & vr6522->reg[VIA_REG_DDR_B]);  
      break;
    case VIA_REG_PORT_A:
      viaIfrResetBit(vr6522, 0x03);
      viaPsgBus(vr6522);
      return (vr6522->IRA & (~vr6522->reg[VIA_REG_DDR_A])) | (vr6522->reg[VIA_REG_PORT_A] & vr6522->reg[VIA_REG_DDR_A]);      
      break;
    case VIA_REG_DDR_B:
      break;
    case VIA_REG_DDR_A:
      break;

    case VIA_REG_T1C_L:
      viaIfrResetBit(vr6522, VIA_IFR_T1);
      break;

    case VIA_REG_T2C_L:
      vr6522->t2Active = false;
      viaIfrResetBit(vr6522, VIA_IFR_T2);
      break;

    case VIA_REG_PORT_Ax:
      viaPsgBus(vr6522);
      return (vr6522->IRA & (~vr6522->reg[VIA_REG_DDR_A])) | (vr6522->reg[VIA_REG_PORT_Ax] & vr6522->reg[VIA_REG_DDR_A]);
      break;


  }

  return value;
}
*/

/*
 * multiple clock ticks
 */
void __not_in_flash("vrEmu6522Ticks") vrEmu6522Ticks(VrEmu6522* vr6522, int ticks) {
  if (vr6522->t1Active)
  {
    int32_t t1 = (vr6522->reg[VIA_REG_T1C_H] << 8) | vr6522->reg[VIA_REG_T1C_L];
    t1 -= ticks;
    if (t1 <= 0)
    {
      viaIfrSetBit(vr6522, VIA_IFR_T1);
      if (vr6522->reg[VIA_REG_ACR] & 0x40)
      {
        uint32_t latchVal = (vr6522->reg[VIA_REG_T1L_H] << 8) | vr6522->reg[VIA_REG_T1L_L];
        latchVal += t1;
        vr6522->reg[VIA_REG_T1C_H] = (latchVal & 0xff00) >> 8;
        vr6522->reg[VIA_REG_T1C_L] = (latchVal & 0x00ff);
      }
    }
    else
    {
      vr6522->reg[VIA_REG_T1C_H] = (t1 & 0xff00) >> 8;
      vr6522->reg[VIA_REG_T1C_L] = (t1 & 0x00ff);
    }
  }

  if (vr6522->t2Active)
  {
    int32_t t2 = (vr6522->reg[VIA_REG_T2C_H] << 8) | vr6522->reg[VIA_REG_T2C_L];
    t2 -= ticks;
    if (t2 <= 0)
    {
      viaIfrSetBit(vr6522, VIA_IFR_T2);
      // one-shot just continues counting (from 0xffff)
      uint16_t newT2 = (vr6522->reg[VIA_REG_T2C_H] << 8) | vr6522->reg[VIA_REG_T2C_L];
      newT2 -= ticks;
      vr6522->reg[VIA_REG_T2C_H] = (newT2 & 0xff00) >> 8;
      vr6522->reg[VIA_REG_T2C_L] = (newT2 & 0x00ff);
    }
    else
    {
      vr6522->reg[VIA_REG_T2C_H] = (t2 & 0xff00) >> 8;
      vr6522->reg[VIA_REG_T2C_L] = (t2 & 0x00ff);
    }
  }
}

bool __not_in_flash("vrEmu6522Int") vrEmu6522Int(VrEmu6522* vr6522) {
  return vr6522->intPin;
}

void __not_in_flash("vrEmu6522PressKeyMtx") vrEmu6522PressKeyMtx(VrEmu6522* vr6522,uint8_t line, uint8_t bcol) {
    if (line < 8) {
        vr6522->kM[line] |= bcol;
    }
}
void __not_in_flash("vrEmu6522ReleaseKeyMtx") vrEmu6522ReleaseKeyMtx(VrEmu6522* vr6522,uint8_t line, uint8_t bcol) {
    if (line < 8) {
        vr6522->kM[line] &= ~bcol;
    }
}

void __not_in_flash("v") vrEmu6522Joystick(VrEmu6522* vr6522, uint8_t value){
    vr6522->joy = value;
}
