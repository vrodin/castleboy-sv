/* host shim for supervision.h - lets us compile the port with gcc for testing */
#ifndef SUPERVISION_H_SHIM
#define SUPERVISION_H_SHIM
extern unsigned char SV_VRAM_MEM[0x2000];
#define SV_VIDEO (SV_VRAM_MEM)
extern unsigned char SV_CONTROL_REG;
#define SV_CONTROL (SV_CONTROL_REG)
/* host stub for the bank register (banking is a no-op on the host) */
extern unsigned char SV_BANK_REG;
#define SV_BANK (SV_BANK_REG)
struct sv_lcd_s { unsigned char width, height, xpos, ypos; };
extern struct sv_lcd_s SV_LCD;
#define JOY_UP_MASK    0x08
#define JOY_DOWN_MASK  0x04
#define JOY_LEFT_MASK  0x02
#define JOY_RIGHT_MASK 0x01
#define JOY_BTN1_MASK  0x20
#define JOY_BTN2_MASK  0x10
#define JOY_BTN3_MASK  0x80
#define JOY_BTN4_MASK  0x40
#endif
