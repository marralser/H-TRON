/*
   JOYTEST2.C
   Safer Amstrad PC1512 / PC1640 raw joystick diagnostic
   Borland Turbo C / DOS

   IMPORTANT:
   This version performs the Amstrad keyboard ACK sequence:
      read scan code from port 60h
      pulse bit 7 of port 61h high, then restore it
      send EOI to 8259 PIC

   Confirmed joystick make codes so far:
      Right  = 79h
      Left   = 7Ah
      Fire B = 78h
*/

#include <stdio.h>
#include <conio.h>
#include <dos.h>

#define KBD_DATA_PORT  0x60
#define SYS_PORT_B     0x61
#define PIC_CMD_PORT   0x20
#define PIC_EOI        0x20

#define BUF_SIZE 64

volatile unsigned char rawbuf[BUF_SIZE];
volatile unsigned int raw_head = 0;
volatile unsigned int raw_tail = 0;

void interrupt (*old_kbd_isr)(void);

void interrupt raw_kbd_isr(void)
{
    unsigned char sc;
    unsigned char pb;

    /* Read the latched Amstrad keyboard/joystick code. */
    sc = inp(KBD_DATA_PORT);

    rawbuf[raw_head] = sc;
    raw_head = (raw_head + 1) % BUF_SIZE;

    /*
       AMSTRAD PC1512/PC1640 keyboard acknowledge sequence.

       PB7=1 disables the keyboard data path / keyboard interrupt.
       Restore PB7 to its previous cleared state afterwards.
    */
    pb = inp(SYS_PORT_B);
    outp(SYS_PORT_B, pb | 0x80);
    outp(SYS_PORT_B, pb & 0x7F);

    /* End IRQ1 at the master 8259 PIC. */
    outp(PIC_CMD_PORT, PIC_EOI);
}

int raw_available(void)
{
    return raw_head != raw_tail;
}

unsigned char raw_get(void)
{
    unsigned char sc;

    disable();
    sc = rawbuf[raw_tail];
    raw_tail = (raw_tail + 1) % BUF_SIZE;
    enable();

    return sc;
}

int main(void)
{
    unsigned char sc;

    clrscr();
    printf("JOYTEST2 - Amstrad PC1512/PC1640 raw joystick test\n");
    printf("--------------------------------------------------\n\n");
    printf("This version uses the Amstrad port-61h keyboard ACK.\n\n");
    printf("Move one direction at a time and release it.\n");
    printf("Press/release each fire button.\n");
    printf("Press ESC to exit (raw scan code 01h).\n\n");
    printf("Expected make codes already confirmed:\n");
    printf("  RIGHT = 79   LEFT = 7A   FIRE B = 78\n\n");
    printf("Raw codes:\n\n");

    raw_head = raw_tail = 0;

    old_kbd_isr = getvect(0x09);
    setvect(0x09, raw_kbd_isr);

    for (;;)
    {
        if (raw_available())
        {
            sc = raw_get();
            printf("%02X ", sc);

            if (sc == 0x01)
                break;
        }
    }

    disable();
    setvect(0x09, old_kbd_isr);
    enable();

    printf("\n\nOriginal INT 09h restored.\n");
    printf("Press a key to return to DOS.\n");
    getch();

    return 0;
}
