/***************************************************************************
    vtech2.c

    system driver
    Juergen Buchmueller <pullmoll@t-online.de> MESS driver, Jan 2000
    Davide Moretti <dave@rimini.com> ROM dump and hardware description

    LASER 350 (it has only 16K of RAM)
    FFFF|-------|
        | Empty |
        |   5   |
    C000|-------|
        |  RAM  |
        |   3   |
    8000|-------|-------|-------|
        |  ROM  |Display|  I/O  |
        |   1   |   3   |   2   |
    4000|-------|-------|-------|
        |  ROM  |
        |   0   |
    0000|-------|


    Laser 500/700 with 64K of RAM and
    Laser 350 with 64K RAM expansion module
    FFFF|-------|
        |  RAM  |
        |   5   |
    C000|-------|
        |  RAM  |
        |   4   |
    8000|-------|-------|-------|
        |  ROM  |Display|  I/O  |
        |   1   |   7   |   2   |
    4000|-------|-------|-------|
        |  ROM  |
        |   0   |
    0000|-------|


    Bank "|main|"       Contents
    0    0x00000 - 0x03fff ROM 1st half
    1    0x04000 - 0x07fff ROM 2nd half
    2           n/a        I/O 2KB area (mirrored 8 times?)
    3    0x0c000 - 0x0ffff Display RAM (16KB) present in Laser 350 only!
    4    0x10000 - 0x13fff RAM #4
    5    0x14000 - 0x17fff RAM #5
    6    0x18000 - 0x1bfff RAM #6
    7    0x1c000 - 0x1ffff RAM #7 (Display RAM with 64KB)
    8    0x20000 - 0x23fff RAM #8 (Laser 700 or 128KB extension)
    9    0x24000 - 0x27fff RAM #9
    A    0x28000 - 0x2bfff RAM #A
    B    0x2c000 - 0x2ffff RAM #B
    C    0x30000 - 0x33fff ROM expansion
    D    0x34000 - 0x34fff ROM expansion
    E    0x38000 - 0x38fff ROM expansion
    F    0x3c000 - 0x3ffff ROM expansion

    TODO:
        Add ROMs and drivers for the Laser100, 110,
        210 and 310 machines and the Texet 8000.
        They should probably go to the vtech1.c files, though.

***************************************************************************/

#include "driver.h"
#include "includes/vtech2.h"
#include "devices/cartslot.h"
#include "devices/cassette.h"
#include "formats/vt_cas.h"

static ADDRESS_MAP_START(vtech2_mem, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x3fff) AM_READWRITE(SMH_BANK1, SMH_BANK1)
	AM_RANGE(0x4000, 0x7fff) AM_READWRITE(SMH_BANK2, SMH_BANK2)
	AM_RANGE(0x8000, 0xbfff) AM_READWRITE(SMH_BANK3, SMH_BANK3)
	AM_RANGE(0xc000, 0xffff) AM_READWRITE(SMH_BANK4, SMH_BANK4)
ADDRESS_MAP_END

static ADDRESS_MAP_START(vtech2_io, ADDRESS_SPACE_IO, 8)
	ADDRESS_MAP_GLOBAL_MASK(0xff)
	AM_RANGE(0x10, 0x1f) AM_READWRITE(laser_fdc_r, laser_fdc_w)
	AM_RANGE(0x40, 0x43) AM_WRITE(laser_bank_select_w)
	AM_RANGE(0x44, 0x44) AM_WRITE(laser_bg_mode_w)
	AM_RANGE(0x45, 0x45) AM_WRITE(laser_two_color_w)
ADDRESS_MAP_END

/* 2008-05 FP: 
Small note about natural keyboard: currently,
- "Graph" is mapped to 'F11'
- "Del Line" is mapped to 'F12'
*/
static INPUT_PORTS_START( laser500 )
	PORT_START_TAG("ROW0")	/* KEY ROW 0 */
    PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_LSHIFT) PORT_CODE(KEYCODE_RSHIFT) PORT_CHAR(UCHAR_SHIFT_1)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Z) 			PORT_CHAR('z') PORT_CHAR('Z')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_X) 			PORT_CHAR('x') PORT_CHAR('X')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_C) 			PORT_CHAR('c') PORT_CHAR('C')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_V) 			PORT_CHAR('v') PORT_CHAR('V')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_B) 			PORT_CHAR('b') PORT_CHAR('B')
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_N) 			PORT_CHAR('n') PORT_CHAR('N')

    PORT_START_TAG("ROW1") /* KEY ROW 1 */
    PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_LCONTROL)		PORT_CHAR(UCHAR_SHIFT_2)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_A) 			PORT_CHAR('a') PORT_CHAR('A')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_S) 			PORT_CHAR('s') PORT_CHAR('S')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_D) 			PORT_CHAR('d') PORT_CHAR('D')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F) 			PORT_CHAR('f') PORT_CHAR('F')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_G) 			PORT_CHAR('g') PORT_CHAR('G')
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_H) 			PORT_CHAR('h') PORT_CHAR('H')

    PORT_START_TAG("ROW2") /* KEY ROW 2 */
    PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_TAB) 			PORT_CHAR('\t')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Q) 			PORT_CHAR('q') PORT_CHAR('Q')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_W) 			PORT_CHAR('w') PORT_CHAR('W')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_E) 			PORT_CHAR('e') PORT_CHAR('E')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_R) 			PORT_CHAR('r') PORT_CHAR('R')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_T) 			PORT_CHAR('t') PORT_CHAR('T')
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Y) 			PORT_CHAR('y') PORT_CHAR('Y')

    PORT_START_TAG("ROW3") /* KEY ROW 3 */
    PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_ESC) 			PORT_CHAR(UCHAR_MAMEKEY(ESC))
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_1) 			PORT_CHAR('1') PORT_CHAR('!')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_2) 			PORT_CHAR('2') PORT_CHAR('@')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_3) 			PORT_CHAR('3') PORT_CHAR('#')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_4) 			PORT_CHAR('4') PORT_CHAR('$')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_5) 			PORT_CHAR('5') PORT_CHAR('%')
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_6) 			PORT_CHAR('6') PORT_CHAR('^')

	PORT_START_TAG("ROW4") /* KEY ROW 4 */
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_EQUALS)		PORT_CHAR('=') PORT_CHAR('+')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_MINUS)		PORT_CHAR('-') PORT_CHAR('_')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_0) 			PORT_CHAR('0') PORT_CHAR(')')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_9) 			PORT_CHAR('9') PORT_CHAR('(')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_8) 			PORT_CHAR('8') PORT_CHAR('*')
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_7) 			PORT_CHAR('7') PORT_CHAR('&')

	PORT_START_TAG("ROW5") /* KEY ROW 5 */
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("BS") PORT_CODE(KEYCODE_BACKSPACE) PORT_CHAR(8)
    PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_P) 			PORT_CHAR('p') PORT_CHAR('P')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_O) 			PORT_CHAR('o') PORT_CHAR('O')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_I) 			PORT_CHAR('i') PORT_CHAR('I')
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_U) 			PORT_CHAR('u') PORT_CHAR('U')

	PORT_START_TAG("ROW6") /* KEY ROW 6 */
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Return") PORT_CODE(KEYCODE_ENTER) PORT_CHAR(13)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_QUOTE) 		PORT_CHAR('\'') PORT_CHAR('"')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_COLON) 		PORT_CHAR(';') PORT_CHAR(':')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_L) 			PORT_CHAR('l') PORT_CHAR('L')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_K) 			PORT_CHAR('k') PORT_CHAR('K')
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_J) 			PORT_CHAR('j') PORT_CHAR('J')

	PORT_START_TAG("ROW7") /* KEY ROW 7 */
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Graph") PORT_CODE(KEYCODE_LALT) PORT_CHAR(UCHAR_MAMEKEY(F11))
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSLASH)	PORT_CHAR('`') PORT_CHAR('~')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_SPACE) 		PORT_CHAR(' ')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_SLASH) 		PORT_CHAR('/') PORT_CHAR('?')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_STOP) 		PORT_CHAR('.') PORT_CHAR('>')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_COMMA) 		PORT_CHAR(',') PORT_CHAR('<')
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_M) 			PORT_CHAR('m') PORT_CHAR('M')

    PORT_START_TAG("ROWA") /* KEY ROW A */
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F1) 			PORT_CHAR(UCHAR_MAMEKEY(F1))
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F2) 			PORT_CHAR(UCHAR_MAMEKEY(F2))
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F3) 			PORT_CHAR(UCHAR_MAMEKEY(F3))
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F4) 			PORT_CHAR(UCHAR_MAMEKEY(F4))
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START_TAG("ROWB") /* KEY ROW B */
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F10) 			PORT_CHAR(UCHAR_MAMEKEY(F10))
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F9) 			PORT_CHAR(UCHAR_MAMEKEY(F9))
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F8) 			PORT_CHAR(UCHAR_MAMEKEY(F8))
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F7) 			PORT_CHAR(UCHAR_MAMEKEY(F7))
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F6) 			PORT_CHAR(UCHAR_MAMEKEY(F6))
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F5) 			PORT_CHAR(UCHAR_MAMEKEY(F5))

	PORT_START_TAG("ROWC") /* KEY ROW C */
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Cap Lock") PORT_CODE(KEYCODE_CAPSLOCK)	PORT_CHAR(UCHAR_MAMEKEY(CAPSLOCK))
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Del Line") PORT_CODE(KEYCODE_PGUP)		PORT_CHAR(UCHAR_MAMEKEY(F12))
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_HOME) 		PORT_CHAR(UCHAR_MAMEKEY(HOME))
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_UP)			PORT_CHAR(UCHAR_MAMEKEY(UP))
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_LEFT)			PORT_CHAR(UCHAR_MAMEKEY(LEFT))
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_RIGHT) 		PORT_CHAR(UCHAR_MAMEKEY(RIGHT))
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_DOWN)			PORT_CHAR(UCHAR_MAMEKEY(DOWN))

	PORT_START_TAG("ROWD") /* KEY ROW D */
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSLASH2)	PORT_CHAR('\\') PORT_CHAR('|')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_CLOSEBRACE)	PORT_CHAR(']') PORT_CHAR('}')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_OPENBRACE)	PORT_CHAR('[') PORT_CHAR('{')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Mu  \xC2\xA3") PORT_CODE(KEYCODE_TILDE) PORT_CHAR('\xA3')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Del") PORT_CODE(KEYCODE_DEL)				PORT_CHAR(UCHAR_MAMEKEY(DEL))
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Ins") PORT_CODE(KEYCODE_INSERT)			PORT_CHAR(UCHAR_MAMEKEY(INSERT))
INPUT_PORTS_END

/* 2008-05 FP: I wasn't able to find a good picture of the laser 350 to verify the mapping of the emulated keyboard. 
However, old-computers.com describes it as a laser 500/700 in a laser 300/310 case. The missing inputs seem to 
confirm this. */
static INPUT_PORTS_START( laser350 )
	PORT_INCLUDE( laser500 )

    PORT_MODIFY("ROW2") /* KEY ROW 2 */
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNUSED) /* TAB not on the Laser350 */

    PORT_MODIFY("ROW3") /* KEY ROW 3 */
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNUSED) /* ESC not on the Laser350 */

	PORT_MODIFY("ROW5") /* KEY ROW 5 */
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNUSED) /* BS not on the Laser350 */

	PORT_MODIFY("ROW7") /* KEY ROW 7 */
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNUSED) /* GRAPH not on the Laser350 */

    PORT_MODIFY("ROWA") /* KEY ROW A */
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED) /* not on the Laser350 */

	PORT_MODIFY("ROWB") /* KEY ROW B */
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED) /* not on the Laser350 */

	PORT_MODIFY("ROWC") /* KEY ROW C */
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED) /* not on the Laser350 */

	PORT_MODIFY("ROWD") /* KEY ROW D */
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED) /* not on the Laser350 */

	/* 2008-05 FP: This input_port seems never to be read. Is it a leftover of the old cassette code? */
	PORT_START_TAG("TAPE") /* Tape control */
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Tape Start") PORT_CODE(KEYCODE_SLASH_PAD)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Tape Stop") PORT_CODE(KEYCODE_ASTERISK)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Tape Rewind") PORT_CODE(KEYCODE_MINUS_PAD)
	PORT_BIT(0x1f, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END


static const gfx_layout charlayout_80 =
{
	8,8,					/* 8 x 8 characters */
	256,					/* 256 characters */
	1,                      /* 1 bits per pixel */
	{ 0 },                  /* no bitplanes; 1 bit per pixel */
	/* x offsets */
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	/* y offsets */
	{ 0*8,1*8,2*8,3*8,4*8,5*8,6*8,7*8 },
	8*8 					/* every char takes 8 bytes */
};

static const gfx_layout charlayout_40 =
{
	8*2,8,					/* 8*2 x 8 characters */
	256,					/* 256 characters */
	1,                      /* 1 bits per pixel */
	{ 0 },                  /* no bitplanes; 1 bit per pixel */
	/* x offsets */
	{ 0,0, 1,1, 2,2, 3,3, 4,4, 5,5, 6,6, 7,7 },
	/* y offsets */
	{ 0*8,1*8,2*8,3*8,4*8,5*8,6*8,7*8 },
	8*8 					/* every char takes 8 bytes */
};

static const gfx_layout gfxlayout_1bpp =
{
	8,1,					/* 8x1 pixels */
	256,					/* 256 codes */
	1,						/* 1 bit per pixel */
	{ 0 },					/* no bitplanes */
	/* x offsets */
	{ 7,6,5,4,3,2,1,0 },
	/* y offsets */
	{ 0 },
	8						/* one byte per code */
};

static const gfx_layout gfxlayout_1bpp_dw =
{
	8*2,1,					/* 8 times 2x1 pixels */
	256,					/* 256 codes */
	1,						/* 1 bit per pixel */
	{ 0 },					/* no bitplanes */
	/* x offsets */
	{ 7,7,6,6,5,5,4,4,3,3,2,2,1,1,0,0 },
	/* y offsets */
	{ 0 },
	8						/* one byte per code */
};

static const gfx_layout gfxlayout_1bpp_qw =
{
	8*4,1,					/* 8 times 4x1 pixels */
	256,					/* 256 codes */
	1,						/* 1 bit per pixel */
	{ 0 },					/* no bitplanes */
	/* x offsets */
	{ 7,7,7,7,6,6,6,6,5,5,5,5,4,4,4,4,3,3,3,3,2,2,2,2,1,1,1,1,0,0,0,0 },
	/* y offsets */
	{ 0 },
	8						/* one byte per code */
};

static const gfx_layout gfxlayout_4bpp =
{
	2*4,1,					/* 2 times 4x1 pixels */
	256,					/* 256 codes */
	4,						/* 4 bit per pixel */
	{ 0,1,2,3 },			/* four bitplanes */
	/* x offsets */
	{ 4,4,4,4, 0,0,0,0 },
	/* y offsets */
	{ 0 },
	2*4 					/* one byte per code */
};

static const gfx_layout gfxlayout_4bpp_dh =
{
	2*4,2,					/* 2 times 4x2 pixels */
	256,					/* 256 codes */
	4,						/* 4 bit per pixel */
	{ 0,1,2,3 },			/* four bitplanes */
	/* x offsets */
	{ 4,4,4,4, 0,0,0,0 },
	/* y offsets */
	{ 0,0 },
	2*4 					/* one byte per code */
};

static GFXDECODE_START( vtech2 )
	GFXDECODE_ENTRY( "gfx1", 0, charlayout_80, 0, 256 )
	GFXDECODE_ENTRY( "gfx1", 0, charlayout_40, 0, 256 )
	GFXDECODE_ENTRY( "gfx2", 0, gfxlayout_1bpp, 0, 256 )
	GFXDECODE_ENTRY( "gfx2", 0, gfxlayout_1bpp_dw, 0, 256 )
	GFXDECODE_ENTRY( "gfx2", 0, gfxlayout_1bpp_qw, 0, 256 )
	GFXDECODE_ENTRY( "gfx2", 0, gfxlayout_4bpp, 2*256, 1 )
	GFXDECODE_ENTRY( "gfx2", 0, gfxlayout_4bpp_dh, 2*256, 1 )
GFXDECODE_END


static const rgb_t vt_colors[] =
{
	RGB_BLACK,
	MAKE_RGB(0x00, 0x00, 0x7f),  /* blue */
	MAKE_RGB(0x00, 0x7f, 0x00),  /* green */
	MAKE_RGB(0x00, 0x7f, 0x7f),  /* cyan */
	MAKE_RGB(0x7f, 0x00, 0x00),  /* red */
	MAKE_RGB(0x7f, 0x00, 0x7f),  /* magenta */
	MAKE_RGB(0x7f, 0x7f, 0x00),  /* yellow */
	MAKE_RGB(0xa0, 0xa0, 0xa0),  /* bright grey */
	MAKE_RGB(0x7f, 0x7f, 0x7f),  /* dark grey */
	MAKE_RGB(0x00, 0x00, 0xff),  /* bright blue */
	MAKE_RGB(0x00, 0xff, 0x00),  /* bright green */
	MAKE_RGB(0x00, 0xff, 0xff),  /* bright cyan */
	MAKE_RGB(0xff, 0x00, 0x00),  /* bright red */
	MAKE_RGB(0xff, 0x00, 0xff),  /* bright magenta */
	MAKE_RGB(0xff, 0xff, 0x00),  /* bright yellow */
	RGB_WHITE
};


/* Initialise the palette */
static PALETTE_INIT( vtech2 )
{
	int i;

	machine->colortable = colortable_alloc(machine, 16);

	for ( i = 0; i < 16; i++ )
		colortable_palette_set_color(machine->colortable, i, vt_colors[i]);

	for (i = 0; i < 256; i++)
	{
		colortable_entry_set_value(machine->colortable, 2*i, i&15);
		colortable_entry_set_value(machine->colortable, 2*i+1, i>>4);
	}

	for (i = 0; i < 16; i++)
		colortable_entry_set_value(machine->colortable, 512+i, i);
}

static INTERRUPT_GEN( vtech2_interrupt )
{
	cpunum_set_input_line(machine, 0, 0, HOLD_LINE);
}

static MACHINE_DRIVER_START( laser350 )
	/* basic machine hardware */
	MDRV_CPU_ADD("main", Z80, 3694700)        /* 3.694700 Mhz */
	MDRV_CPU_PROGRAM_MAP(vtech2_mem, 0)
	MDRV_CPU_IO_MAP(vtech2_io, 0)
	MDRV_CPU_VBLANK_INT("main", vtech2_interrupt)
	MDRV_SCREEN_ADD("main", RASTER)
	MDRV_SCREEN_REFRESH_RATE(50)
	MDRV_SCREEN_VBLANK_TIME(0)
	MDRV_INTERLEAVE(1)

	MDRV_MACHINE_RESET( laser350 )

    /* video hardware */
	MDRV_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
	MDRV_SCREEN_SIZE(88*8, 24*8+32)
	MDRV_SCREEN_VISIBLE_AREA(0*8, 88*8-1, 0*8, 24*8+32-1)
	MDRV_GFXDECODE( vtech2 )
	MDRV_PALETTE_LENGTH(528)
	MDRV_PALETTE_INIT(vtech2)

	MDRV_VIDEO_START(laser)
	MDRV_VIDEO_UPDATE(laser)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")
	MDRV_SOUND_ADD("wave", WAVE, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.50)
	MDRV_SOUND_ADD("speaker", SPEAKER, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.75)
MACHINE_DRIVER_END


static MACHINE_DRIVER_START( laser500 )
	MDRV_IMPORT_FROM( laser350 )
	MDRV_MACHINE_RESET( laser500 )
MACHINE_DRIVER_END


static MACHINE_DRIVER_START( laser700 )
	MDRV_IMPORT_FROM( laser350 )
	MDRV_MACHINE_RESET( laser700 )
MACHINE_DRIVER_END


ROM_START(laser350)
	ROM_REGION(0x40000,"main",0)
	ROM_LOAD("laserv3.rom", 0x00000, 0x08000, CRC(9bed01f7) SHA1(3210fddfab2f4c7855fa902fb8e2fc18d10d48f1))
	ROM_REGION(0x00800,"gfx1",0)
	ROM_LOAD("laser.fnt",   0x00000, 0x00800, CRC(ed6bfb2a) SHA1(95e247021a10167b9de1d6ffc91ec4ba83b0ec87))
	ROM_REGION(0x00100,"gfx2",ROMREGION_ERASEFF)
    /* initialized in init_laser */
ROM_END


ROM_START(laser500)
	ROM_REGION(0x40000,"main",0)
	ROM_LOAD("laserv3.rom", 0x00000, 0x08000, CRC(9bed01f7) SHA1(3210fddfab2f4c7855fa902fb8e2fc18d10d48f1))
	ROM_REGION(0x00800,"gfx1",0)
	ROM_LOAD("laser.fnt",   0x00000, 0x00800, CRC(ed6bfb2a) SHA1(95e247021a10167b9de1d6ffc91ec4ba83b0ec87))
	ROM_REGION(0x00100,"gfx2",ROMREGION_ERASEFF)
	/* initialized in init_laser */
ROM_END

ROM_START(laser700)
	ROM_REGION(0x40000,"main",0)
	ROM_LOAD("laserv3.rom", 0x00000, 0x08000, CRC(9bed01f7) SHA1(3210fddfab2f4c7855fa902fb8e2fc18d10d48f1))
	ROM_REGION(0x00800,"gfx1",0)
	ROM_LOAD("laser.fnt",   0x00000, 0x00800, CRC(ed6bfb2a) SHA1(95e247021a10167b9de1d6ffc91ec4ba83b0ec87))
	ROM_REGION(0x00100,"gfx2",ROMREGION_ERASEFF)
	/* initialized in init_laser */
ROM_END


/***************************************************************************

  Game driver(s)

***************************************************************************/

static void laser_cassette_getinfo(const mess_device_class *devclass, UINT32 state, union devinfo *info)
{
	/* cassette */
	switch(state)
	{
		/* --- the following bits of info are returned as 64-bit signed integers --- */
		case MESS_DEVINFO_INT_COUNT:							info->i = 1; break;

		/* --- the following bits of info are returned as pointers to data or functions --- */
		case MESS_DEVINFO_PTR_CASSETTE_FORMATS:				info->p = (void *) vtech2_cassette_formats; break;

		default:										cassette_device_getinfo(devclass, state, info); break;
	}
}

static void laser_cartslot_getinfo(const mess_device_class *devclass, UINT32 state, union devinfo *info)
{
	/* cartslot */
	switch(state)
	{
		/* --- the following bits of info are returned as 64-bit signed integers --- */
		case MESS_DEVINFO_INT_COUNT:							info->i = 1; break;

		/* --- the following bits of info are returned as pointers to data or functions --- */
		case MESS_DEVINFO_PTR_LOAD:							info->load = DEVICE_IMAGE_LOAD_NAME(laser_cart); break;
		case MESS_DEVINFO_PTR_UNLOAD:						info->unload = DEVICE_IMAGE_UNLOAD_NAME(laser_cart); break;

		/* --- the following bits of info are returned as NULL-terminated strings --- */
		case MESS_DEVINFO_STR_FILE_EXTENSIONS:				strcpy(info->s = device_temp_str(), "rom"); break;

		default:										cartslot_device_getinfo(devclass, state, info); break;
	}
}

static void laser_floppy_getinfo(const mess_device_class *devclass, UINT32 state, union devinfo *info)
{
	/* floppy */
	switch(state)
	{
		/* --- the following bits of info are returned as 64-bit signed integers --- */
		case MESS_DEVINFO_INT_TYPE:							info->i = IO_FLOPPY; break;
		case MESS_DEVINFO_INT_READABLE:						info->i = 1; break;
		case MESS_DEVINFO_INT_WRITEABLE:						info->i = 0; break;
		case MESS_DEVINFO_INT_CREATABLE:						info->i = 0; break;
		case MESS_DEVINFO_INT_COUNT:							info->i = 2; break;

		/* --- the following bits of info are returned as pointers to data or functions --- */
		case MESS_DEVINFO_PTR_LOAD:							info->load = DEVICE_IMAGE_LOAD_NAME(laser_floppy); break;

		/* --- the following bits of info are returned as NULL-terminated strings --- */
		case MESS_DEVINFO_STR_FILE_EXTENSIONS:				strcpy(info->s = device_temp_str(), "dsk"); break;
	}
}

static SYSTEM_CONFIG_START(laser)
	CONFIG_DEVICE(laser_cassette_getinfo)
	CONFIG_DEVICE(laser_cartslot_getinfo)
	CONFIG_DEVICE(laser_floppy_getinfo)
SYSTEM_CONFIG_END

/*    YEAR   NAME      PARENT    COMPAT MACHINE   INPUT     INIT      CONFIG    COMPANY              FULLNAME */
COMP( 1984?, laser350, 0,		 0,		laser350, laser350, laser,    laser,	"Video Technology",  "Laser 350" , 0)
COMP( 1984?, laser500, laser350, 0,		laser500, laser500, laser,    laser,	"Video Technology",  "Laser 500" , 0)
COMP( 1984?, laser700, laser350, 0,		laser700, laser500, laser,    laser,	"Video Technology",  "Laser 700" , 0)
