/******************************************************************************

    amstrad.c
    system driver

        Amstrad Hardware:
            - 8255 connected to AY-3-8912 sound generator,
                keyboard, cassette, printer, crtc vsync output,
                and some PCB links
            - 6845 (either HD6845S, UM6845R or M6845) crtc graphics display
              controller
            - NEC765 floppy disc controller (CPC664,CPC6128)
            - Z80 CPU running at 4 MHz (slowed by wait states on memory
              access)
            - custom ASIC "Gate Array" controlling rom paging, ram paging,
                current display mode and colour palette

    Kevin Thacker [MESS driver]

  May-June 2004 - Yoann Courtois (aka Papagayo/ex GMC/ex PARADOX) - rewriting some code with hardware documentation from http://www.cepece.info/amstrad/

  June 2006  - Very preliminary CPC+ support.  CPR cart image handling, secondary ROM register, ASIC unlock detection
               Supported:
               12-bit palette,
               12-bit hardware sprites (largely works from what I've seen, some games have display issues),
               Programmable Raster Interrupt (seems to work),
               Split screen registers,
               Soft scroll registers (only byte-by-byre horizontally for now),
               Analogue controls (may well be completely wrong, I have no idea on how these should work),
               Vectored interrupts for Z80 interrupt mode 2 (used by Pang),
               DMA sound channels (may still be some issues, noticable in Navy Seals and Copter 271)
               04/07/06:  Added interrupt vector support for IM 2.
                          Added soft scroll register implementation.  Vertical adjustments are a bit shaky.
               05/07/06:  Fixed hardware sprite offsets
               14/07/06:  Added basic analogue control support.
               04/08/06:  Fixed PRI and Split screen scanline offsets (based on code in Arnold ;))
                          Implemented DMA sound channels
               06/08/06:  Fixed CRTC palette if the ASIC was re-locked after already being unlocked and used.
                          This fixes Klax, which is now playable.
               08/08/06:  Fixed up vertical soft scroll, now we just need to get a finer detail on horizontal soft scroll
                          (Only works on a byte level for now)
                          Fixed DMA pause function when the prescaler is set to 0.

               Tested with the Arnold 5 Diagnostic Cartridge.  Mostly works fine, but the soft scroll test is
               noticably wrong.

               Known issues with some games (as at 12/01/08):
               Robocop 2:  playable, but sprites should be cut off outside the playing area (partial updates should be able to fix this).
               Navy Seals:  Playable, but sound effects cut out at times (probably DMA related).
               Dick Tracy:  Sprite visibility issues
               Switchblade:  has some slowdown when numerous enemies are on screen (normal?)
               Epyx World of Sports: doesn't start at all.
               Tennis Cup II:  controls don't seem to work.
               Fire and Forget II:  playable, but the top half of the screen flickers
               Crazy Cars II:  playable, with slight shaking of horizon
               No Exit:  Display is wrong, but usable, uses demo-like techniques.


   January 2008 - added preliminary Aleste 520EX support
               The Aleste 520EX is a Russian clone of the CPC6128, that expands on existing video mode, and can run MSX-DOS.
               It also adds an MC146818 RTC/NVRAM, an Intel 8253 timer, and the "Magic Sound" board, a 4-channel DMA-based
               sample player.  Also includes a software emulation of the MSX2 VDP, used in the ports of MSX games.

               Known issues:
                - The RTC doesn't always update in setup.  It expects bit 4 of register C (Update End Interrupt) to be high.
                - Title screens appear then disappear quickly (probably because the 8253 hasn't been plugged in yet).
                - Some video modes display wrong (still needs some work here).
                - Vampire Killer crashes after collecting a certain key part way through stage 1.
                - Magic Sound isn't emulated.


   January 2009 - changed drivers to use the mc6845 device implementation
               To get rid of duplicated code the drivers have been changed to use the new mc6845 device
               implementation. As a result the (runtime) selection of CRTC type has been removed.


Some bugs left :
----------------
    - CRTC all type support (0,1,2,3,4) ?
    - Gate Array and CRTC aren't synchronised. (The Gate Array can change the color every microseconds?) So the multi-rasters in one line aren't supported (see yao demo p007's part)!
    - Implement full Asic for CPC+ emulation.  Soft scroll is rather dodgy.
    - The KC Compact should not reuse the gate array functionality. Instead z8536 support should be added. (bug #42)

 ******************************************************************************/

/* Core includes */
#include "driver.h"
#include "includes/amstrad.h"

/* Components */
#include "machine/i8255a.h"	/* for 8255 ppi */
#include "cpu/z80/z80.h"		/* for cycle tables */
#include "video/mc6845.h"		/* CRTC */
#include "machine/nec765.h"	/* for floppy disc controller */
#include "sound/ay8910.h"
#include "sound/wave.h"
#include "machine/mc146818.h"  /* Aleste RTC */
#include "machine/ctronics.h"

/* Devices */
#include "devices/flopdrv.h"
#include "formats/basicdsk.h"
#include "includes/msx_slot.h"
#include "includes/msx.h"  /* MSX floppy device load */
#include "devices/snapquik.h"
#include "devices/cartslot.h"
#include "devices/cassette.h"
#include "formats/tzx_cas.h"


#define MANUFACTURER_NAME 0x07
#define TV_REFRESH_RATE 0x10

#define SYSTEM_CPC    0
#define SYSTEM_PLUS   1
#define SYSTEM_GX4000 2



/* -----------------------------
   - amstrad_ppi8255_interface -
   -----------------------------*/
static I8255A_INTERFACE( amstrad_ppi8255_interface )
{
	DEVCB_HANDLER(amstrad_ppi_porta_r),	/* port A read */
	DEVCB_HANDLER(amstrad_ppi_portb_r),	/* port B read */
	DEVCB_NULL,							/* port C read */
	DEVCB_HANDLER(amstrad_ppi_porta_w),	/* port A write */
	DEVCB_NULL,							/* port B write */
	DEVCB_HANDLER(amstrad_ppi_portc_w)	/* port C write */
};


/* Amstrad NEC765 interface doesn't use interrupts or DMA! */
static const nec765_interface amstrad_nec765_interface =
{
	DEVCB_NULL,
	NULL,
	NULL,
	NEC765_RDY_PIN_CONNECTED,
	{FLOPPY_0,FLOPPY_1, NULL, NULL}
};

/* Aleste uses an 8272A, with the interrupt flag visible on PPI port B */
static const nec765_interface aleste_8272_interface =
{
	DEVCB_LINE(aleste_interrupt),
	NULL,
	NULL,
	NEC765_RDY_PIN_CONNECTED,
	{FLOPPY_0,FLOPPY_1, NULL, NULL}
};


static DRIVER_INIT( aleste )
{
	mc146818_init(machine, MC146818_IGNORE_CENTURY);
}


/* Memory is banked in 16k blocks. However, the multiface
pages the memory in 8k blocks! The ROM can
be paged into bank 0 and bank 3. */
static ADDRESS_MAP_START(amstrad_mem, ADDRESS_SPACE_PROGRAM, 8)
	AM_RANGE(0x00000, 0x01fff) AM_READWRITE(SMH_BANK(1), SMH_BANK(9))
	AM_RANGE(0x02000, 0x03fff) AM_READWRITE(SMH_BANK(2), SMH_BANK(10))
	AM_RANGE(0x04000, 0x05fff) AM_READWRITE(SMH_BANK(3), SMH_BANK(11))
	AM_RANGE(0x06000, 0x07fff) AM_READWRITE(SMH_BANK(4), SMH_BANK(12))
	AM_RANGE(0x08000, 0x09fff) AM_READWRITE(SMH_BANK(5), SMH_BANK(13))
	AM_RANGE(0x0a000, 0x0bfff) AM_READWRITE(SMH_BANK(6), SMH_BANK(14))
	AM_RANGE(0x0c000, 0x0dfff) AM_READWRITE(SMH_BANK(7), SMH_BANK(15))
	AM_RANGE(0x0e000, 0x0ffff) AM_READWRITE(SMH_BANK(8), SMH_BANK(16))
ADDRESS_MAP_END

/* I've handled the I/O ports in this way, because the ports
are not fully decoded by the CPC h/w. Doing it this way means
I can decode it myself and a lot of  software should work */
static ADDRESS_MAP_START(amstrad_io, ADDRESS_SPACE_IO, 8)
	AM_RANGE(0x0000, 0xffff) AM_READWRITE( amstrad_cpc_io_r, amstrad_cpc_io_w )
ADDRESS_MAP_END


/*************************************
 *
 *  Input ports
 *
 *************************************/

static INPUT_PORTS_START( amstrad_keyboard )
	/* keyboard row 0 */
	PORT_START("keyboard_row_0")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xE2\x86\x91")          PORT_CODE(KEYCODE_UP)         PORT_CHAR(UCHAR_MAMEKEY(UP))
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xE2\x86\x92")          PORT_CODE(KEYCODE_RIGHT)      PORT_CHAR(UCHAR_MAMEKEY(RIGHT))
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xE2\x86\x93")          PORT_CODE(KEYCODE_DOWN)       PORT_CHAR(UCHAR_MAMEKEY(DOWN))
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad 9")              PORT_CODE(KEYCODE_9_PAD)      PORT_CHAR(UCHAR_MAMEKEY(9_PAD))
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad 6")              PORT_CODE(KEYCODE_6_PAD)      PORT_CHAR(UCHAR_MAMEKEY(6_PAD))
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad 3")              PORT_CODE(KEYCODE_3_PAD)      PORT_CHAR(UCHAR_MAMEKEY(3_PAD))
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad Enter")          PORT_CODE(KEYCODE_ENTER_PAD)  PORT_CHAR(UCHAR_MAMEKEY(ENTER_PAD))
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad .")              PORT_CODE(KEYCODE_DEL_PAD)    PORT_CHAR(UCHAR_MAMEKEY(DEL_PAD))

	/* keyboard line 1 */
	PORT_START("keyboard_row_1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xE2\x86\x90")          PORT_CODE(KEYCODE_LEFT)       PORT_CHAR(UCHAR_MAMEKEY(LEFT))
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Copy")                  PORT_CODE(KEYCODE_END)        PORT_CHAR(UCHAR_MAMEKEY(END))
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad 7")              PORT_CODE(KEYCODE_7_PAD)      PORT_CHAR(UCHAR_MAMEKEY(7_PAD))
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad 8")              PORT_CODE(KEYCODE_8_PAD)      PORT_CHAR(UCHAR_MAMEKEY(8_PAD))
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad 5")              PORT_CODE(KEYCODE_5_PAD)      PORT_CHAR(UCHAR_MAMEKEY(5_PAD))
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad 1")              PORT_CODE(KEYCODE_1_PAD)      PORT_CHAR(UCHAR_MAMEKEY(1_PAD))
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad 2")              PORT_CODE(KEYCODE_2_PAD)      PORT_CHAR(UCHAR_MAMEKEY(2_PAD))
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad 0")              PORT_CODE(KEYCODE_0_PAD)      PORT_CHAR(UCHAR_MAMEKEY(0_PAD))

	/* keyboard row 2 */
	PORT_START("keyboard_row_2")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Clr")                   PORT_CODE(KEYCODE_BACKSPACE)  PORT_CHAR(UCHAR_MAMEKEY(HOME))
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("[")                     PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR('[') PORT_CHAR('{')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Enter")                 PORT_CODE(KEYCODE_ENTER)      PORT_CHAR(13)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("]")                     PORT_CODE(KEYCODE_BACKSLASH)  PORT_CHAR(']') PORT_CHAR('}')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad 4")              PORT_CODE(KEYCODE_4_PAD)      PORT_CHAR(UCHAR_MAMEKEY(4_PAD))
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Shift")                 PORT_CODE(KEYCODE_LSHIFT) PORT_CODE(KEYCODE_RSHIFT)     PORT_CHAR(UCHAR_SHIFT_1)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\\")                    PORT_CODE(KEYCODE_RCONTROL)   PORT_CHAR('\\') PORT_CHAR('`')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Ctrl")                  PORT_CODE(KEYCODE_RALT)       PORT_CHAR(UCHAR_SHIFT_2)

	/* keyboard row 3 */
	PORT_START("keyboard_row_3")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xE2\x86\x91 \xC2\xA3") PORT_CODE(KEYCODE_EQUALS)     PORT_CHAR('^') PORT_CHAR(0xa3)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_MINUS)      PORT_CHAR('-') PORT_CHAR('=')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("@ \xC2\xA6")            PORT_CODE(KEYCODE_OPENBRACE)  PORT_CHAR('@') PORT_CHAR('|')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_P)          PORT_CHAR('p') PORT_CHAR('P')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_QUOTE)      PORT_CHAR(';') PORT_CHAR('+')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_COLON)      PORT_CHAR(':') PORT_CHAR('*')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_SLASH)      PORT_CHAR('/') PORT_CHAR('?')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_STOP)       PORT_CHAR('.') PORT_CHAR('>')

	/* keyboard line 4 */
	PORT_START("keyboard_row_4")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_0)          PORT_CHAR('0') PORT_CHAR('_')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_9)          PORT_CHAR('9') PORT_CHAR(')')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_O)          PORT_CHAR('o') PORT_CHAR('O')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_I)          PORT_CHAR('i') PORT_CHAR('I')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_L)          PORT_CHAR('l') PORT_CHAR('L')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_K)          PORT_CHAR('k') PORT_CHAR('K')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_M)          PORT_CHAR('m') PORT_CHAR('M')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_COMMA)      PORT_CHAR(',') PORT_CHAR('<')

	/* keyboard line 5 */
	PORT_START("keyboard_row_5")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_8)          PORT_CHAR('8') PORT_CHAR('(')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_7)          PORT_CHAR('7') PORT_CHAR('\'')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_U)          PORT_CHAR('u') PORT_CHAR('U')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_Y)          PORT_CHAR('y') PORT_CHAR('Y')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_H)          PORT_CHAR('h') PORT_CHAR('H')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_J)          PORT_CHAR('j') PORT_CHAR('J')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_N)          PORT_CHAR('n') PORT_CHAR('N')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Space")                 PORT_CODE(KEYCODE_SPACE)      PORT_CHAR(32)

	/* keyboard line 6 */
	PORT_START("keyboard_row_6")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_6)          PORT_CHAR('6') PORT_CHAR('&')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_5)          PORT_CHAR('5') PORT_CHAR('%')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_R)          PORT_CHAR('r') PORT_CHAR('R')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_T)          PORT_CHAR('t') PORT_CHAR('T')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_G)          PORT_CHAR('g') PORT_CHAR('G')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_F)          PORT_CHAR('f') PORT_CHAR('F')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_B)          PORT_CHAR('b') PORT_CHAR('B')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_V)          PORT_CHAR('v') PORT_CHAR('V')

	/* keyboard line 7 */
	PORT_START("keyboard_row_7")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_4)          PORT_CHAR('4') PORT_CHAR('$')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_3)          PORT_CHAR('3') PORT_CHAR('#')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_E)          PORT_CHAR('e') PORT_CHAR('E')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_W)          PORT_CHAR('w') PORT_CHAR('W')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_S)          PORT_CHAR('s') PORT_CHAR('S')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_D)          PORT_CHAR('d') PORT_CHAR('D')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_C)          PORT_CHAR('c') PORT_CHAR('C')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_X)          PORT_CHAR('x') PORT_CHAR('X')

	/* keyboard line 8 */
	PORT_START("keyboard_row_8")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_1)          PORT_CHAR('1') PORT_CHAR('!')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_2)          PORT_CHAR('2') PORT_CHAR('\"') PORT_CHAR('~')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Esc")                   PORT_CODE(KEYCODE_TILDE)      PORT_CHAR(27)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_Q)          PORT_CHAR('q') PORT_CHAR('Q')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Tab")                   PORT_CODE(KEYCODE_TAB)        PORT_CHAR(9)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_A)          PORT_CHAR('a') PORT_CHAR('A')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Caps Lock")             PORT_CODE(KEYCODE_CAPSLOCK)   PORT_CHAR(UCHAR_MAMEKEY(CAPSLOCK)) PORT_TOGGLE
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_Z)          PORT_CHAR('z') PORT_CHAR('Z')

	/* keyboard line 9 */
	PORT_START("keyboard_row_9")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP)    PORT_PLAYER(1) PORT_8WAY
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN)  PORT_PLAYER(1) PORT_8WAY
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT)  PORT_PLAYER(1) PORT_8WAY
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT) PORT_PLAYER(1) PORT_8WAY
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1)        PORT_PLAYER(1)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2)        PORT_PLAYER(1)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNUSED)
/* The bit for the third button is officially undocumented: Amstrad joysticks actually
   use only two buttons. The only device that reads this bit is the AMX mouse, which uses
   dedicated hardware to connect to the joystick port.
*/
//  PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_BUTTON3)        PORT_PLAYER(1)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Del")                   PORT_CODE(KEYCODE_INSERT)     PORT_CHAR(8)

/* Second joystick port need to be defined: the second joystick is daisy-chained on the back of the first one
   Joystick 2 is mapped to keyboard line 6.  */

/* The CPC supports at least two different brands of lightpens, probably connected to the expansion port */

INPUT_PORTS_END


/* Steph 2000-10-27 I remapped the 'Machine Name' Dip Switches (easier to understand) */
static INPUT_CHANGED( cpc_monitor_changed )
{
	running_machine *machine = field->port->machine;
	const UINT8	*color_prom = NULL;

	if ( (input_port_read(machine, "green_display")) & 0x01 )
	{
		PALETTE_INIT_CALL( amstrad_cpc_green );
	}
	else
	{
		PALETTE_INIT_CALL( amstrad_cpc );
	}
}


static INPUT_PORTS_START( crtc_links )

/* the following are defined as dipswitches, but are in fact solder links on the
 * curcuit board. The links are open or closed when the PCB is made, and are set depending on which country
 * the Amstrad system was to go to (reference: http://amstrad.cpc.free.fr/article.php?sid=26)

lk1 lk2 lk3 Manufacturer Name (CPC and CPC+ only):

0   0   0   Isp
0   0   1   Triumph (UK?)
0   1   0   Saisho (UK: Saisho is Dixons brand name for their electronic goods)
0   1   1   Solavox
1   0   0   Awa (Australia)
1   0   1   Schneider (Germany)
1   1   0   Orion (Japan?)
1   1   1   Amstrad (UK)

lk4     Frequency
0       60 Hz
1       50 Hz
*/
	PORT_START("solder_links")
	PORT_DIPNAME(0x07, 0x07, "Manufacturer Name")
	PORT_DIPLOCATION("LK:3,2,1")
	PORT_DIPSETTING(0x00, "Isp")
	PORT_DIPSETTING(0x01, "Triumph")
	PORT_DIPSETTING(0x02, "Saisho")
	PORT_DIPSETTING(0x03, "Solavox")
	PORT_DIPSETTING(0x04, "Awa")
	PORT_DIPSETTING(0x05, "Schneider")
	PORT_DIPSETTING(0x06, "Orion")
	PORT_DIPSETTING(0x07, "Amstrad")

	PORT_DIPNAME(0x10, 0x10, "TV Refresh Rate")
	PORT_DIPLOCATION("LK:4")
	PORT_DIPSETTING(0x00, "60 Hz")
	PORT_DIPSETTING(0x10, "50 Hz")

/* Part number Manufacturer Type number
   UM6845      UMC          0
   HD6845S     Hitachi      0
   UM6845R     UMC          1
   MC6845      Motorola     2
   AMS40489    Amstrad      3
   Pre-ASIC??? Amstrad?     4 In the "cost-down" CPC6128, the CRTC functionality is integrated into a single ASIC IC. This ASIC is often refered to as the "Pre-ASIC" because it preceeded the CPC+ ASIC
As far as I know, the KC compact used HD6845S only.
*/
//  PORT_START("crtc")
//  PORT_CONFNAME( 0xFF, M6845_PERSONALITY_UM6845R, "CRTC Type")
//  PORT_CONFSETTING(M6845_PERSONALITY_UM6845, "Type 0 - UM6845")
//  PORT_CONFSETTING(M6845_PERSONALITY_HD6845S, "Type 0 - HD6845S")
//  PORT_CONFSETTING(M6845_PERSONALITY_UM6845R, "Type 1 - UM6845R")
//  PORT_CONFSETTING(M6845_PERSONALITY_GENUINE, "Type 2 - MC6845")
//  PORT_CONFSETTING(M6845_PERSONALITY_AMS40489, "Type 3 - AMS40489")
//  PORT_CONFSETTING(M6845_PERSONALITY_PREASIC, "Type 4 - Pre-ASIC")

	PORT_START("multiface")
	PORT_CONFNAME(0x01, 0x00, "Multiface Two" )
	PORT_CONFSETTING(0x00, DEF_STR( Off) )
	PORT_CONFSETTING(0x01, DEF_STR( On) )
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Multiface Two's Stop Button") PORT_CODE(KEYCODE_F1)
//  PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Multiface Two's Reset Button") PORT_CODE(KEYCODE_F3)  Not implemented

	PORT_START("green_display")
	PORT_CONFNAME( 0x01, 0x00, "Monitor" ) PORT_CHANGED( cpc_monitor_changed, 0 )
	PORT_CONFSETTING(0x00, "CTM640 Colour Monitor" )
	PORT_CONFSETTING(0x01, "GT64 Green Monitor" )

INPUT_PORTS_END


static INPUT_PORTS_START( cpc464 )
	PORT_INCLUDE(amstrad_keyboard)
	PORT_INCLUDE(crtc_links)
INPUT_PORTS_END


static INPUT_PORTS_START( cpc664 )
	PORT_INCLUDE(amstrad_keyboard)

	PORT_MODIFY("keyboard_row_0")
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad f9")             PORT_CODE(KEYCODE_9_PAD)      PORT_CHAR(UCHAR_MAMEKEY(9_PAD))
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad f6")             PORT_CODE(KEYCODE_6_PAD)      PORT_CHAR(UCHAR_MAMEKEY(6_PAD))
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad f3")             PORT_CODE(KEYCODE_3_PAD)      PORT_CHAR(UCHAR_MAMEKEY(3_PAD))

	PORT_MODIFY("keyboard_row_1")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad f7")             PORT_CODE(KEYCODE_7_PAD)      PORT_CHAR(UCHAR_MAMEKEY(7_PAD))
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad f8")             PORT_CODE(KEYCODE_8_PAD)      PORT_CHAR(UCHAR_MAMEKEY(8_PAD))
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad f5")             PORT_CODE(KEYCODE_5_PAD)      PORT_CHAR(UCHAR_MAMEKEY(5_PAD))
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad f1")             PORT_CODE(KEYCODE_1_PAD)      PORT_CHAR(UCHAR_MAMEKEY(1_PAD))
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad f2")             PORT_CODE(KEYCODE_2_PAD)      PORT_CHAR(UCHAR_MAMEKEY(2_PAD))
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad f0")             PORT_CODE(KEYCODE_0_PAD)      PORT_CHAR(UCHAR_MAMEKEY(0_PAD))

	PORT_MODIFY("keyboard_row_2")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR('[') PORT_CHAR('{')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_BACKSLASH)  PORT_CHAR(']') PORT_CHAR('}')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keypad f4")             PORT_CODE(KEYCODE_4_PAD)      PORT_CHAR(UCHAR_MAMEKEY(4_PAD))

	PORT_INCLUDE(crtc_links)
INPUT_PORTS_END


static INPUT_PORTS_START( cpc6128 )
	PORT_INCLUDE(amstrad_keyboard)

	PORT_MODIFY("keyboard_row_0")
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f9")                    PORT_CODE(KEYCODE_9_PAD)      PORT_CHAR(UCHAR_MAMEKEY(9_PAD))
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f6")                    PORT_CODE(KEYCODE_6_PAD)      PORT_CHAR(UCHAR_MAMEKEY(6_PAD))
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f3")                    PORT_CODE(KEYCODE_3_PAD)      PORT_CHAR(UCHAR_MAMEKEY(3_PAD))
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Enter")                 PORT_CODE(KEYCODE_RALT)       PORT_CHAR(UCHAR_MAMEKEY(ENTER_PAD))
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(".")                     PORT_CODE(KEYCODE_DEL_PAD)    PORT_CHAR(UCHAR_MAMEKEY(DEL_PAD))

	PORT_MODIFY("keyboard_row_1")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Copy")                  PORT_CODE(KEYCODE_LALT)       PORT_CHAR(UCHAR_MAMEKEY(END))
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f7")                    PORT_CODE(KEYCODE_7_PAD)      PORT_CHAR(UCHAR_MAMEKEY(7_PAD))
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f8")                    PORT_CODE(KEYCODE_8_PAD)      PORT_CHAR(UCHAR_MAMEKEY(8_PAD))
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f5")                    PORT_CODE(KEYCODE_5_PAD)      PORT_CHAR(UCHAR_MAMEKEY(5_PAD))
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f1")                    PORT_CODE(KEYCODE_1_PAD)      PORT_CHAR(UCHAR_MAMEKEY(1_PAD))
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f2")                    PORT_CODE(KEYCODE_2_PAD)      PORT_CHAR(UCHAR_MAMEKEY(2_PAD))
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f0")                    PORT_CODE(KEYCODE_0_PAD)      PORT_CHAR(UCHAR_MAMEKEY(0_PAD))

	PORT_MODIFY("keyboard_row_2")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR('[') PORT_CHAR('{')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Return")                PORT_CODE(KEYCODE_ENTER)      PORT_CHAR(13)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_BACKSLASH)  PORT_CHAR(']') PORT_CHAR('}')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f4")                    PORT_CODE(KEYCODE_4_PAD)      PORT_CHAR(UCHAR_MAMEKEY(4_PAD))
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_RCONTROL)   PORT_CHAR('\\') PORT_CHAR('`')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Control")               PORT_CODE(KEYCODE_LCONTROL)   PORT_CHAR(UCHAR_SHIFT_2)

	PORT_INCLUDE(crtc_links)
INPUT_PORTS_END


/* This CPC6128 sold in France has the AZERTY layout. Reference: http://amstrad.cpc.free.fr/amstrad/cpc6128.htm */
static INPUT_PORTS_START( cpc6128f )
	PORT_INCLUDE(amstrad_keyboard)

	PORT_MODIFY("keyboard_row_0")
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f9")                    PORT_CODE(KEYCODE_9_PAD)      PORT_CHAR(UCHAR_MAMEKEY(9_PAD))
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f6")                    PORT_CODE(KEYCODE_6_PAD)      PORT_CHAR(UCHAR_MAMEKEY(6_PAD))
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f3")                    PORT_CODE(KEYCODE_3_PAD)      PORT_CHAR(UCHAR_MAMEKEY(3_PAD))
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Enter")                 PORT_CODE(KEYCODE_RALT)       PORT_CHAR(UCHAR_MAMEKEY(ENTER_PAD))
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(".")                     PORT_CODE(KEYCODE_DEL_PAD)    PORT_CHAR(UCHAR_MAMEKEY(DEL_PAD))

	PORT_MODIFY("keyboard_row_1")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Copy")                  PORT_CODE(KEYCODE_LALT)       PORT_CHAR(UCHAR_MAMEKEY(END))
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f7")                    PORT_CODE(KEYCODE_7_PAD)      PORT_CHAR(UCHAR_MAMEKEY(7_PAD))
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f8")                    PORT_CODE(KEYCODE_8_PAD)      PORT_CHAR(UCHAR_MAMEKEY(8_PAD))
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f5")                    PORT_CODE(KEYCODE_5_PAD)      PORT_CHAR(UCHAR_MAMEKEY(5_PAD))
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f1")                    PORT_CODE(KEYCODE_1_PAD)      PORT_CHAR(UCHAR_MAMEKEY(1_PAD))
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f2")                    PORT_CODE(KEYCODE_2_PAD)      PORT_CHAR(UCHAR_MAMEKEY(2_PAD))
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f0")                    PORT_CODE(KEYCODE_0_PAD)      PORT_CHAR(UCHAR_MAMEKEY(0_PAD))

	PORT_MODIFY("keyboard_row_2")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR('*') PORT_CHAR('<')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Return")                PORT_CODE(KEYCODE_ENTER)      PORT_CHAR(13)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_BACKSLASH)  PORT_CHAR('#') PORT_CHAR('>')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f4")                    PORT_CODE(KEYCODE_4_PAD)      PORT_CHAR(UCHAR_MAMEKEY(4_PAD))
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_RCONTROL)   PORT_CHAR('$') PORT_CHAR('@') PORT_CHAR('\\')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Control")               PORT_CODE(KEYCODE_LCONTROL)   PORT_CHAR(UCHAR_SHIFT_2)

	PORT_MODIFY("keyboard_row_3")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_EQUALS)     PORT_CHAR('-') PORT_CHAR('_')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_MINUS)      PORT_CHAR(')') PORT_CHAR('[')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("^   \xC2\xA6")          PORT_CODE(KEYCODE_OPENBRACE)  PORT_CHAR('^') PORT_CHAR('|')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xC3\xB9   %")          PORT_CODE(KEYCODE_QUOTE)      PORT_CHAR(0xF9) PORT_CHAR('%')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_COLON)      PORT_CHAR('m') PORT_CHAR('M')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_SLASH)      PORT_CHAR('=') PORT_CHAR('+')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_STOP)       PORT_CHAR(':') PORT_CHAR('/')

	PORT_MODIFY("keyboard_row_4")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xC3\xA0   0")          PORT_CODE(KEYCODE_0)          PORT_CHAR(0xE0) PORT_CHAR('0')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xC3\xA7   9")          PORT_CODE(KEYCODE_9)          PORT_CHAR(0xE7) PORT_CHAR('9')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_M)          PORT_CHAR(',') PORT_CHAR('?')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_COMMA)      PORT_CHAR(';') PORT_CHAR('.')

	PORT_MODIFY("keyboard_row_5")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_8)          PORT_CHAR('!') PORT_CHAR('8')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xC3\xA9   7")          PORT_CODE(KEYCODE_7)          PORT_CHAR(0xE8) PORT_CHAR('7')

	PORT_MODIFY("keyboard_row_6")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_6)          PORT_CHAR(']') PORT_CHAR('6')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_5)          PORT_CHAR('(') PORT_CHAR('5')

	PORT_MODIFY("keyboard_row_7")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_4)          PORT_CHAR('\'') PORT_CHAR('4')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_3)          PORT_CHAR('\"') PORT_CHAR('3')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_W)          PORT_CHAR('z') PORT_CHAR('Z')

	PORT_MODIFY("keyboard_row_8")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_1)          PORT_CHAR('&') PORT_CHAR('1')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xC3\xA9   2   ~")      PORT_CODE(KEYCODE_2)          PORT_CHAR(0xE9) PORT_CHAR('2') PORT_CHAR('~')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_Q)          PORT_CHAR('a') PORT_CHAR('A')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_A)          PORT_CHAR('q') PORT_CHAR('Q')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_Z)          PORT_CHAR('w') PORT_CHAR('W')

	PORT_INCLUDE(crtc_links)
INPUT_PORTS_END

static INPUT_PORTS_START( cpc6128s )
	PORT_INCLUDE(cpc6128)

	PORT_MODIFY("keyboard_row_2")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xC3\x9C")              PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR(0x00DC)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xC3\x89")              PORT_CODE(KEYCODE_BACKSLASH)  PORT_CHAR(0x00E9)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD)									   PORT_CODE(KEYCODE_RCONTROL)   PORT_CHAR('/') PORT_CHAR('?')

	PORT_MODIFY("keyboard_row_3")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD)									   PORT_CODE(KEYCODE_EQUALS)     PORT_CHAR('+') PORT_CHAR('*')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_MINUS)      PORT_CHAR('-') PORT_CHAR('=')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xC3\x85")			   PORT_CODE(KEYCODE_OPENBRACE)	 PORT_CHAR(0x00C5)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xC3\x84")			   PORT_CODE(KEYCODE_QUOTE)		 PORT_CHAR(0x00C4)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\xC3\x96")			   PORT_CODE(KEYCODE_COLON)		 PORT_CHAR(0x00D6)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_SLASH)      PORT_CHAR('<') PORT_CHAR('>')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_STOP)       PORT_CHAR('.') PORT_CHAR(':')

	PORT_MODIFY("keyboard_row_4")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_COMMA)      PORT_CHAR(',') PORT_CHAR(';')
INPUT_PORTS_END

/*
 * The BIOS of the KC Compact would be able to recognize the keypresses
 * generated by F5-F9. Unfortunately these keys are not present on the
 * keyboard! Reference: http://www.cepece.info/amstrad/docs/kcc/kcc01.jpg
 *
 */
static INPUT_PORTS_START( kccomp )
	PORT_INCLUDE(amstrad_keyboard)

	PORT_MODIFY("keyboard_row_0")
	PORT_BIT(0x08, 0x08, IPT_UNUSED)
	PORT_BIT(0x10, 0x10, IPT_UNUSED)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f3")                    PORT_CODE(KEYCODE_3_PAD)      PORT_CHAR(UCHAR_MAMEKEY(3_PAD))
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Enter")                 PORT_CODE(KEYCODE_LCONTROL)   PORT_CHAR(UCHAR_MAMEKEY(ENTER_PAD))
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(".")                     PORT_CODE(KEYCODE_DEL_PAD)    PORT_CHAR(UCHAR_MAMEKEY(DEL_PAD))

	PORT_MODIFY("keyboard_row_1")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Copy")                  PORT_CODE(KEYCODE_RALT)       PORT_CHAR(UCHAR_MAMEKEY(END))
	PORT_BIT(0x04, 0x04, IPT_UNUSED)
	PORT_BIT(0x08, 0x08, IPT_UNUSED)
	PORT_BIT(0x10, 0x10, IPT_UNUSED)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f1")                    PORT_CODE(KEYCODE_1_PAD)      PORT_CHAR(UCHAR_MAMEKEY(1_PAD))
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f2")                    PORT_CODE(KEYCODE_2_PAD)      PORT_CHAR(UCHAR_MAMEKEY(2_PAD))
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f0")                    PORT_CODE(KEYCODE_0_PAD)      PORT_CHAR(UCHAR_MAMEKEY(0_PAD))

	PORT_MODIFY("keyboard_row_2")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR('[') PORT_CHAR('{')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Return")                PORT_CODE(KEYCODE_ENTER)      PORT_CHAR(13)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_BACKSLASH)  PORT_CHAR(']') PORT_CHAR('}')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("f4")                    PORT_CODE(KEYCODE_4_PAD)      PORT_CHAR(UCHAR_MAMEKEY(4_PAD))
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD)                                    PORT_CODE(KEYCODE_RCONTROL)   PORT_CHAR('\\') PORT_CHAR('`')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Control")               PORT_CODE(KEYCODE_LALT)       PORT_CHAR(UCHAR_SHIFT_2)

	PORT_INCLUDE(crtc_links)
INPUT_PORTS_END


static INPUT_PORTS_START( plus )
	PORT_INCLUDE(amstrad_keyboard)
	PORT_INCLUDE(crtc_links)

	/* The CPC+ and GX4000 adds support for analogue controllers.
       Up to two joysticks or four paddles can be used, although the ASIC supports twice that.
       Read at &6808-&680f in ASIC RAM
       I am unsure if these are even close to correct.

    UPDATE: the analog port supports (probably with an Y-cable) up to two analog joysticks
    with two buttons each or four paddles with one button each. The CPC+/GX4000 have also an
    AUX port which supports a lightgun/lightpen with two buttons.
    Since all these devices have their dedicated connector, two digital joystick, two analog joysticks
    and one lightgun can be connected at the same moment.

    The connectors' description for both CPCs and CPC+'s can be found at http://www.hardwarebook.info/Category:Computer */

	PORT_START("analog1")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_X)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(1)

	PORT_START("analog2")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_Y)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(1)

	PORT_START("analog3")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_X)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(2)

	PORT_START("analog4")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_Y)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(2)

// Not used, but are here for completeness
	PORT_START("analog5")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_X)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(3)

	PORT_START("analog6")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_Y)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(3)

	PORT_START("analog7")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_X)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(4)

	PORT_START("analog8")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_Y)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(4)
INPUT_PORTS_END


static INPUT_PORTS_START( gx4000 )

	// The GX4000 is a console, so no keyboard access other than the joysticks.
	PORT_START("keyboard_row_0")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("keyboard_row_1")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("keyboard_row_2")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("keyboard_row_3")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("keyboard_row_4")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("keyboard_row_5")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("keyboard_row_6")  // Joystick 2
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP)    PORT_PLAYER(2) PORT_8WAY
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN)  PORT_PLAYER(2) PORT_8WAY
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT)  PORT_PLAYER(2) PORT_8WAY
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT) PORT_PLAYER(2) PORT_8WAY
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1)        PORT_PLAYER(2)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2)        PORT_PLAYER(2)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("keyboard_row_7")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("keyboard_row_8")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("keyboard_row_9")  // Joystick 1
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP)    PORT_PLAYER(1) PORT_8WAY
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN)  PORT_PLAYER(1) PORT_8WAY
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT)  PORT_PLAYER(1) PORT_8WAY
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT) PORT_PLAYER(1) PORT_8WAY
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1)        PORT_PLAYER(1)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2)        PORT_PLAYER(1)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_INCLUDE(crtc_links)  // included to keep the driver happy

	PORT_START("analog1")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_X)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(1)

	PORT_START("analog2")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_Y)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(1)

	PORT_START("analog3")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_X)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(2)

	PORT_START("analog4")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_Y)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(2)

// Not used, but are here for completeness
	PORT_START("analog5")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_X)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(3)

	PORT_START("analog6")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_Y)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(3)

	PORT_START("analog7")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_X)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(4)

	PORT_START("analog8")
	PORT_BIT(0x3f , 0, IPT_TRACKBALL_Y)
	PORT_SENSITIVITY(100)
	PORT_KEYDELTA(10)
	PORT_PLAYER(4)
INPUT_PORTS_END


static INPUT_PORTS_START( aleste )
	PORT_INCLUDE(amstrad_keyboard)

	PORT_MODIFY( "keyboard_row_9" )
	/* Documentation marks this input as "R/L", it's purpose is unknown - I can't even find it on the keyboard */
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("R  L")       PORT_CODE(KEYCODE_PGUP)

	PORT_START( "keyboard_row_10" )
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD)  PORT_NAME("F1  F6")    PORT_CODE(KEYCODE_F1)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD)  PORT_NAME("F2  F7")    PORT_CODE(KEYCODE_F2)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD)  PORT_NAME("F3  F8")    PORT_CODE(KEYCODE_F3)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD)  PORT_NAME("F4  F9")    PORT_CODE(KEYCODE_F4)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD)  PORT_NAME("F5  F10")   PORT_CODE(KEYCODE_F5)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD)  PORT_NAME("HELP")      PORT_CODE(KEYCODE_PGDN)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD)	 PORT_NAME("INS")       PORT_CODE(KEYCODE_DEL)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD)  PORT_NAME("Funny looking Russian symbol")     PORT_CODE(KEYCODE_END)

	PORT_INCLUDE(crtc_links)
	PORT_MODIFY("multiface")  // move Multiface II stop to F6, as the Aleste has it's own F1-F5 keys.
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Multiface Two's Stop Button") PORT_CODE(KEYCODE_F6)
INPUT_PORTS_END


/* --------------------
   - ay8910_interface -
   --------------------*/
static const ay8910_interface ay8912_interface =
{
	AY8910_LEGACY_OUTPUT,
	AY8910_DEFAULT_LOADS,
	DEVCB_MEMORY_HANDLER("maincpu", PROGRAM, amstrad_psg_porta_read),	/* portA read */
	DEVCB_MEMORY_HANDLER("maincpu", PROGRAM, amstrad_psg_porta_read),	/* portB read */
	DEVCB_NULL,					/* portA write */
	DEVCB_NULL					/* portB write */
};


static const gfx_layout asic_sprite_layout =
{
	16,16,
	16,
	4,
	{ 4,5,6,7 },
	{ 0,8,16,24,32,40,48,56,64,72,80,88,96,104,112,120 },
   	{ 0*128, 1*128, 2*128, 3*128, 4*128, 5*128, 6*128, 7*128, 8*128, 9*128, 10*128, 11*128, 12*128, 13*128, 14*128, 15*128 },
	16*16*8
};

static GFXDECODE_START( asic_sprite )
	GFXDECODE_ENTRY( "user1", 0, asic_sprite_layout, 32, 1 )
GFXDECODE_END



/*************************************
 *
 *  Machine drivers
 *
 *************************************/

/* actual clock to CPU is 4 MHz, but it is slowed by memory
accessess. A HALT is used for every memory access by the CPU.
This stretches the timing for opcodes, and gives an effective
speed of 3.8 MHz */

/* Info about structures below:

    The Amstrad has a CPU running at 4 MHz, slowed with wait states.
    I have measured 19968 NOP instructions per frame, which gives,
    50.08 fps as the tv refresh rate.

  There are 312 lines on a PAL screen, giving 64us per line.

  There is only 50us visible per line, and 35*8 lines visible on the screen.

  This is the reason why the displayed area is not the same as the visible area.
 */


static const cassette_config amstrad_cassette_config =
{
	cdt_cassette_formats,
	NULL,
	CASSETTE_STOPPED | CASSETTE_MOTOR_DISABLED | CASSETTE_SPEAKER_ENABLED
};

static const floppy_config cpc6128_floppy_config =
{
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	FLOPPY_DRIVE_SS_40,
	FLOPPY_OPTIONS_NAME(default),
	DO_NOT_KEEP_GEOMETRY
};

static const floppy_config aleste_floppy_config =
{
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	FLOPPY_DRIVE_DS_80,
	FLOPPY_OPTIONS_NAME(msx),
	DO_NOT_KEEP_GEOMETRY
};

static MACHINE_DRIVER_START( cpcplus_cartslot )
	MDRV_CARTSLOT_ADD("cart")
	MDRV_CARTSLOT_EXTENSION_LIST("cpr,bin")
	MDRV_CARTSLOT_MANDATORY
	MDRV_CARTSLOT_LOAD(amstrad_plus_cartridge)
MACHINE_DRIVER_END

static MACHINE_DRIVER_START( amstrad )
	/* Machine hardware */
	MDRV_CPU_ADD("maincpu", Z80, XTAL_16MHz / 4)
	MDRV_CPU_PROGRAM_MAP(amstrad_mem)
	MDRV_CPU_IO_MAP(amstrad_io)

	MDRV_QUANTUM_TIME(HZ(60))

	MDRV_MACHINE_RESET( amstrad )

	MDRV_I8255A_ADD( "ppi8255", amstrad_ppi8255_interface )

    /* video hardware */
	MDRV_SCREEN_ADD("screen", RASTER)
	MDRV_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
	MDRV_SCREEN_RAW_PARAMS( XTAL_16MHz, 1024, 0, 640, 312, 0, 200 )
	MDRV_VIDEO_ATTRIBUTES(VIDEO_ALWAYS_UPDATE)

	MDRV_PALETTE_LENGTH(32)
	MDRV_PALETTE_INIT(amstrad_cpc)

	MDRV_MC6845_ADD( "mc6845", MC6845, XTAL_16MHz / 16, mc6845_amstrad_intf )

	MDRV_VIDEO_START(amstrad)
	MDRV_VIDEO_UPDATE(amstrad)
	MDRV_VIDEO_EOF(amstrad)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")
	MDRV_SOUND_WAVE_ADD("wave", "cassette")
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.50)
	MDRV_SOUND_ADD("ay", AY8912, XTAL_16MHz / 16)
	MDRV_SOUND_CONFIG(ay8912_interface)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.25)

	/* printer */
	MDRV_CENTRONICS_ADD("centronics", standard_centronics)

	/* snapshot */
	MDRV_SNAPSHOT_ADD("snapshot", amstrad, "sna", 0)

	MDRV_CASSETTE_ADD( "cassette", amstrad_cassette_config )

	MDRV_NEC765A_ADD("nec765", amstrad_nec765_interface)

	MDRV_FLOPPY_2_DRIVES_ADD(cpc6128_floppy_config)
MACHINE_DRIVER_END


static MACHINE_DRIVER_START( kccomp )
	MDRV_IMPORT_FROM(amstrad)
	MDRV_MACHINE_RESET(kccomp)

	MDRV_PALETTE_INIT(kccomp)
MACHINE_DRIVER_END


static MACHINE_DRIVER_START( cpcplus )
	/* Machine hardware */
	MDRV_CPU_ADD("maincpu", Z80, XTAL_40MHz / 10)
	MDRV_CPU_PROGRAM_MAP(amstrad_mem)
	MDRV_CPU_IO_MAP(amstrad_io)

	MDRV_QUANTUM_TIME(HZ(60))

	MDRV_MACHINE_START( plus )
	MDRV_MACHINE_RESET( plus )

	MDRV_I8255A_ADD( "ppi8255", amstrad_ppi8255_interface )

	/* video hardware */
	MDRV_SCREEN_ADD("screen", RASTER)
	MDRV_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
	MDRV_SCREEN_RAW_PARAMS( ( XTAL_40MHz * 2 ) / 5, 1024, 0, 640, 312, 0, 200 )
	MDRV_VIDEO_ATTRIBUTES(VIDEO_ALWAYS_UPDATE)

	MDRV_PALETTE_LENGTH(4096)
	MDRV_PALETTE_INIT(amstrad_plus)

	MDRV_MC6845_ADD( "mc6845", MC6845, XTAL_40MHz / 40, mc6845_amstrad_plus_intf )

	MDRV_VIDEO_START(amstrad)
	MDRV_VIDEO_UPDATE(amstrad)
	MDRV_VIDEO_EOF(amstrad)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")
	MDRV_SOUND_WAVE_ADD("wave", "cassette")
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.50)
	MDRV_SOUND_ADD("ay", AY8912, XTAL_40MHz / 40)
	MDRV_SOUND_CONFIG(ay8912_interface)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.25)

	/* printer */
	MDRV_CENTRONICS_ADD("centronics", standard_centronics)

	/* snapshot */
	MDRV_SNAPSHOT_ADD("snapshot", amstrad, "sna", 0)

	MDRV_CASSETTE_ADD( "cassette", amstrad_cassette_config )

	MDRV_NEC765A_ADD("nec765", amstrad_nec765_interface)

	MDRV_IMPORT_FROM(cpcplus_cartslot)

	MDRV_FLOPPY_2_DRIVES_ADD(cpc6128_floppy_config)
MACHINE_DRIVER_END


static MACHINE_DRIVER_START( gx4000 )
	/* Machine hardware */
	MDRV_CPU_ADD("maincpu", Z80, XTAL_40MHz / 10)
	MDRV_CPU_PROGRAM_MAP(amstrad_mem)
	MDRV_CPU_IO_MAP(amstrad_io)

	MDRV_QUANTUM_TIME(HZ(60))

	MDRV_MACHINE_START( plus )
	MDRV_MACHINE_RESET( gx4000 )

	MDRV_I8255A_ADD( "ppi8255", amstrad_ppi8255_interface )

	/* video hardware */
	MDRV_SCREEN_ADD("screen", RASTER)
	MDRV_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
	MDRV_SCREEN_RAW_PARAMS( ( XTAL_40MHz * 2 ) / 5, 1024, 0, 640, 312, 0, 200 )
	MDRV_VIDEO_ATTRIBUTES(VIDEO_ALWAYS_UPDATE)

	MDRV_PALETTE_LENGTH(4096)
	MDRV_PALETTE_INIT(amstrad_plus)

	MDRV_MC6845_ADD( "mc6845", MC6845, XTAL_40MHz / 40, mc6845_amstrad_plus_intf )

	MDRV_VIDEO_START(amstrad)
	MDRV_VIDEO_UPDATE(amstrad)
	MDRV_VIDEO_EOF(amstrad)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")
	MDRV_SOUND_ADD("ay", AY8912, XTAL_40MHz / 40)
	MDRV_SOUND_CONFIG(ay8912_interface)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.25)

	MDRV_IMPORT_FROM(cpcplus_cartslot)
MACHINE_DRIVER_END


static MACHINE_DRIVER_START( aleste )
	MDRV_IMPORT_FROM(amstrad)
	MDRV_MACHINE_RESET(aleste)

	MDRV_SOUND_REPLACE("ay", AY8910, XTAL_16MHz / 16)
	MDRV_SOUND_CONFIG(ay8912_interface)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.25)
	MDRV_PALETTE_LENGTH(32+64)
	MDRV_PALETTE_INIT(aleste)
	MDRV_NVRAM_HANDLER(mc146818)
	MDRV_NEC765A_MODIFY("nec765", aleste_8272_interface)

	MDRV_FLOPPY_2_DRIVES_MODIFY(aleste_floppy_config)
MACHINE_DRIVER_END



/*************************************
 *
 *  ROM definitions
 *
 *************************************/

/* cpc6128.rom contains OS in first 16k, BASIC in second 16k */
/* cpcados.rom contains Amstrad DOS */

/* I am loading the roms outside of the Z80 memory area, because they
are banked. */
ROM_START( cpc6128 )
	/* this defines the total memory size - 64k ram, 16k OS, 16k BASIC, 16k DOS */
	ROM_REGION(0x020000, "maincpu", 0)
	/* load the os to offset 0x01000 from memory base */
	ROM_LOAD("cpc6128.rom", 0x10000, 0x8000, CRC(9e827fe1) SHA1(5977adbad3f7c1e0e082cd02fe76a700d9860c30))
	ROM_LOAD("cpcados.rom", 0x18000, 0x4000, CRC(1fe22ecd) SHA1(39102c8e9cb55fcc0b9b62098780ed4a3cb6a4bb))

	/* optional Multiface hardware */
	ROM_LOAD_OPTIONAL("multface.rom", 0x1c000, 0x2000, CRC(f36086de) SHA1(1431ec628d38f000715545dd2186b684c5fe5a6f))
ROM_END


ROM_START( cpc6128f )
	/* this defines the total memory size (128kb))- 64k ram, 16k OS, 16k BASIC, 16k DOS +16k*/
	ROM_REGION(0x020000, "maincpu", 0)

	/* load the os to offset 0x01000 from memory base */
	ROM_LOAD("cpc6128f.rom", 0x10000, 0x8000, CRC(1574923b) SHA1(200d59076dfef36db061d6d7d21d80021cab1237))
	ROM_LOAD("cpcados.rom",  0x18000, 0x4000, CRC(1fe22ecd) SHA1(39102c8e9cb55fcc0b9b62098780ed4a3cb6a4bb))

	/* optional Multiface hardware */
	ROM_LOAD_OPTIONAL("multface.rom", 0x01c000, 0x2000, CRC(f36086de) SHA1(1431ec628d38f000715545dd2186b684c5fe5a6f))
ROM_END


ROM_START( cpc6128s )
	/* this defines the total memory size (128kb))- 64k ram, 16k OS, 16k BASIC, 16k DOS +16k*/
	ROM_REGION(0x020000, "maincpu", 0)

	/* load the os to offset 0x01000 from memory base */
	ROM_LOAD("cpc6128s.rom", 0x10000, 0x8000, CRC(588b5540) SHA1(6765a91a42fed68a807325bf62a728e5ac5d622f))
	ROM_LOAD("cpcados.rom",  0x18000, 0x4000, CRC(1fe22ecd) SHA1(39102c8e9cb55fcc0b9b62098780ed4a3cb6a4bb))

	/* optional Multiface hardware */
	ROM_LOAD_OPTIONAL("multface.rom", 0x01c000, 0x2000, CRC(f36086de) SHA1(1431ec628d38f000715545dd2186b684c5fe5a6f))
ROM_END


ROM_START( cpc464 )
	/* this defines the total memory size - 64k ram, 16k OS, 16k BASIC, 16k DOS */
	ROM_REGION(0x01c000, "maincpu", 0)
	/* load the os to offset 0x01000 from memory base */
	ROM_LOAD("cpc464.rom",  0x10000, 0x8000, CRC(40852f25) SHA1(56d39c463da60968d93e58b4ba0e675829412a20))
	ROM_LOAD("cpcados.rom", 0x18000, 0x4000, CRC(1fe22ecd) SHA1(39102c8e9cb55fcc0b9b62098780ed4a3cb6a4bb))
ROM_END


ROM_START( cpc664 )
	/* this defines the total memory size - 64k ram, 16k OS, 16k BASIC, 16k DOS */
	ROM_REGION(0x01c000, "maincpu", 0)
	/* load the os to offset 0x01000 from memory base */
	ROM_LOAD("cpc664.rom",  0x10000, 0x8000, CRC(9AB5A036) SHA1(073a7665527b5bd8a148747a3947dbd3328682c8))
	ROM_LOAD("cpcados.rom", 0x18000, 0x4000, CRC(1fe22ecd) SHA1(39102c8e9cb55fcc0b9b62098780ed4a3cb6a4bb))
ROM_END


ROM_START( kccomp )
	ROM_REGION(0x018000, "maincpu", 0)
	ROM_LOAD("kccos.rom",  0x10000, 0x4000, CRC(7f9ab3f7) SHA1(f828045e98e767f737fd93df0af03917f936ad08))
	ROM_LOAD("kccbas.rom", 0x14000, 0x4000, CRC(ca6af63d) SHA1(d7d03755099d0aff501fa5fffc9c0b14c0825448))

	ROM_REGION(0x018000+0x0800, "proms", 0)
	ROM_LOAD("farben.rom", 0x18000, 0x0800, CRC(a50fa3cf) SHA1(2f229ac9f62d56973139dad9992c208421bc0f51))

	/* fake region - required by graphics decode structure */
	/*ROM_REGION(0x0c00, "gfx1") */
ROM_END


/* this system must have a cartridge installed to run */
ROM_START(cpc6128p)
	ROM_REGION(0x80000, "maincpu", ROMREGION_ERASEFF)
	ROM_REGION(0x04000, "user1", ROMREGION_ERASEFF)
ROM_END


#define rom_cpc464p  rom_cpc6128p
#define rom_gx4000  rom_cpc6128p


ROM_START( al520ex )
	ROM_REGION(0x80000, "maincpu", 0)
	ROM_LOAD("al512.bin", 0x10000, 0x10000, CRC(e8c2a9a1) SHA1(ad5827582cb19eaaae1b76e67df62d96da6ad96b))

	ROM_REGION(0x20, "user2", 0)
	ROM_LOAD("af.bin", 0x00, 0x20, CRC(c81fb524) SHA1(17738d0603915a67ec1fddc4cbf7d6b98cdeb8f6))

	ROM_REGION(0x100, "user3", 0)  // RAM bank mappings
	ROM_LOAD("mapper.bin", 0x00, 0x100, CRC(0daebd80) SHA1(8633073cba752c38c5dc912ff9f6a3c89357539b))

	ROM_REGION(0x800, "user4", 0)  // Colour data
	ROM_LOAD("rfcoldat.bin", 0x00, 0x800, CRC(c6ace0e6) SHA1(2f4c51fcfaacb8deed68f6ae9388b870bc962cef))

	ROM_REGION(0x800, "user5", 0)  // Keyboard / Video
	ROM_LOAD("rfvdkey.bin", 0x00, 0x800, CRC(cf2aa4b0) SHA1(20f37da3bc3c377b1c47ae4d9ab8d150faae19a0))

	ROM_REGION(0x100, "user6", 0)
	ROM_LOAD("romram.bin", 0x00, 0x100, CRC(b3ea95d7) SHA1(1252390737a7ead4ecec988c873181798fbc291b))
ROM_END



/*************************************
 *
 *  System configs
 *
 *************************************/
static SYSTEM_CONFIG_START( cpc6128 )
	CONFIG_RAM_DEFAULT(128 * 1024)
SYSTEM_CONFIG_END

static SYSTEM_CONFIG_START( cpcplus )
	CONFIG_IMPORT_FROM(cpc6128)
SYSTEM_CONFIG_END

static SYSTEM_CONFIG_START( gx4000 )
	CONFIG_RAM_DEFAULT(64 * 1024)  // has 64k RAM
SYSTEM_CONFIG_END

static SYSTEM_CONFIG_START( aleste )
	CONFIG_RAM_DEFAULT(2048 * 1024)  // has 2048k RAM
SYSTEM_CONFIG_END

/*************************************
 *
 *  Driver definitions
 *
 *************************************/

/*    YEAR  NAME      PARENT    COMPAT  MACHINE  INPUT     INIT     CONFIG   COMPANY                FULLNAME                                     FLAGS */
COMP( 1984, cpc464,   0,        0,      amstrad, cpc464,   0,       cpc6128, "Amstrad plc",         "Amstrad CPC464",                            0 )
COMP( 1985, cpc664,   cpc464,   0,      amstrad, cpc664,   0,       cpc6128, "Amstrad plc",         "Amstrad CPC664",                            0 )
COMP( 1985, cpc6128,  cpc464,   0,      amstrad, cpc6128,  0,       cpc6128, "Amstrad plc",         "Amstrad CPC6128",                           0 )
COMP( 1985, cpc6128f, cpc464,   0,      amstrad, cpc6128f, 0,       cpc6128, "Amstrad plc",         "Amstrad CPC6128 (France, AZERTY Keyboard)", 0 )
COMP( 1985, cpc6128s, cpc464,   0,      amstrad, cpc6128s, 0,       cpc6128, "Amstrad plc",         "Amstrad CPC6128 (Sweden/Finland)",			 0 )
COMP( 1990, cpc464p,  0,        0,      cpcplus, plus,     0,       cpcplus, "Amstrad plc",         "Amstrad CPC464+",                           0 )
COMP( 1990, cpc6128p, 0,        0,      cpcplus, plus,     0,       cpcplus, "Amstrad plc",         "Amstrad CPC6128+",                          0 )
CONS( 1990, gx4000,   0,        0,      gx4000,  gx4000,   0,       gx4000,  "Amstrad plc",         "Amstrad GX4000",                            0 )
COMP( 1989, kccomp,   cpc464,   0,      kccomp,  kccomp,   0,       cpc6128, "VEB Mikroelektronik", "KC Compact",                                0 )
COMP( 1993, al520ex,  cpc464,   0,      aleste,  aleste,   aleste,  aleste,  "Patisonic",           "Aleste 520EX",                              GAME_IMPERFECT_SOUND )
