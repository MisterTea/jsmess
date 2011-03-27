/**********************************************************************

  machine/nascom1.c

**********************************************************************/

/* Core includes */
#include "emu.h"
#include "includes/nascom1.h"

/* Components */
#include "cpu/z80/z80.h"
#include "machine/wd17xx.h"
#include "machine/ay31015.h"

/* Devices */
#include "imagedev/snapquik.h"
#include "imagedev/cassette.h"
#include "imagedev/flopdrv.h"
#include "machine/ram.h"

#define NASCOM1_KEY_RESET	0x02
#define NASCOM1_KEY_INCR	0x01



/*************************************
 *
 *  Global variables
 *
 *************************************/




/*************************************
 *
 *  Floppy
 *
 *************************************/

static WRITE_LINE_DEVICE_HANDLER( nascom2_fdc_intrq_w )
{
	nascom1_state *drvstate = device->machine->driver_data<nascom1_state>();
	drvstate->nascom2_fdc.irq = state;
}

static WRITE_LINE_DEVICE_HANDLER( nascom2_fdc_drq_w )
{
	nascom1_state *drvstate = device->machine->driver_data<nascom1_state>();
	drvstate->nascom2_fdc.drq = state;
}

const wd17xx_interface nascom2_wd17xx_interface =
{
	DEVCB_LINE_VCC,
	DEVCB_LINE(nascom2_fdc_intrq_w),
	DEVCB_LINE(nascom2_fdc_drq_w),
	{FLOPPY_0, FLOPPY_1, FLOPPY_2, FLOPPY_3}
};


READ8_HANDLER( nascom2_fdc_select_r )
{
	nascom1_state *state = space->machine->driver_data<nascom1_state>();
	return state->nascom2_fdc.select | 0xa0;
}


WRITE8_HANDLER( nascom2_fdc_select_w )
{
	nascom1_state *state = space->machine->driver_data<nascom1_state>();
	device_t *fdc = space->machine->device("wd1793");
	state->nascom2_fdc.select = data;

	logerror("nascom2_fdc_select_w: %02x\n", data);

	if (data & 0x01) wd17xx_set_drive(fdc,0);
	if (data & 0x02) wd17xx_set_drive(fdc,1);
	if (data & 0x04) wd17xx_set_drive(fdc,2);
	if (data & 0x08) wd17xx_set_drive(fdc,3);
	if (data & 0x10) wd17xx_set_side(fdc,(data & 0x10) >> 4);
}


/*
 * D0 -- WD1793 IRQ
 * D1 -- NOT READY
 * D2 to D6 -- 0
 * D7 -- WD1793 DRQ
 *
 */
READ8_HANDLER( nascom2_fdc_status_r )
{
	nascom1_state *state = space->machine->driver_data<nascom1_state>();
	return (state->nascom2_fdc.drq << 7) | state->nascom2_fdc.irq;
}

/*************************************
 *
 *  Keyboard
 *
 *************************************/

READ8_HANDLER ( nascom1_port_00_r )
{
	nascom1_state *state = space->machine->driver_data<nascom1_state>();
	static const char *const keynames[] = { "KEY0", "KEY1", "KEY2", "KEY3", "KEY4", "KEY5", "KEY6", "KEY7", "KEY8" };

	if (state->portstat.stat_count < 9)
		return (input_port_read(space->machine, keynames[state->portstat.stat_count]) | ~0x7f);

	return (0xff);
}


WRITE8_HANDLER( nascom1_port_00_w )
{
	nascom1_state *state = space->machine->driver_data<nascom1_state>();

	cassette_change_state( space->machine->device("cassette"),
		( data & 0x10 ) ? CASSETTE_MOTOR_ENABLED : CASSETTE_MOTOR_DISABLED, CASSETTE_MASK_MOTOR );

	if (!(data & NASCOM1_KEY_RESET))
	{
		if (state->portstat.stat_flags & NASCOM1_KEY_RESET)
			state->portstat.stat_count = 0;
	}
	else
		state->portstat.stat_flags = NASCOM1_KEY_RESET;

	if (!(data & NASCOM1_KEY_INCR))
	{
		if (state->portstat.stat_flags & NASCOM1_KEY_INCR)
			state->portstat.stat_count++;
	}
	else
		state->portstat.stat_flags = NASCOM1_KEY_INCR;
}




/*************************************
 *
 *  Cassette
 *
 *************************************/


READ8_HANDLER( nascom1_port_01_r )
{
	nascom1_state *state = space->machine->driver_data<nascom1_state>();
	return ay31015_get_received_data( state->hd6402 );
}


WRITE8_HANDLER( nascom1_port_01_w )
{
	nascom1_state *state = space->machine->driver_data<nascom1_state>();
	ay31015_set_transmit_data( state->hd6402, data );
}

READ8_HANDLER( nascom1_port_02_r )
{
	nascom1_state *state = space->machine->driver_data<nascom1_state>();
	UINT8 data = 0x31;

	ay31015_set_input_pin( state->hd6402, AY31015_SWE, 0 );
	data |= ay31015_get_output_pin( state->hd6402, AY31015_OR ) ? 0x02 : 0;
	data |= ay31015_get_output_pin( state->hd6402, AY31015_PE ) ? 0x04 : 0;
	data |= ay31015_get_output_pin( state->hd6402, AY31015_FE ) ? 0x08 : 0;
	data |= ay31015_get_output_pin( state->hd6402, AY31015_TBMT ) ? 0x40 : 0;
	data |= ay31015_get_output_pin( state->hd6402, AY31015_DAV ) ? 0x80 : 0;
	ay31015_set_input_pin( state->hd6402, AY31015_SWE, 1 );

	return data;
}


READ8_DEVICE_HANDLER( nascom1_hd6402_si )
{
	return 1;
}


WRITE8_DEVICE_HANDLER( nascom1_hd6402_so )
{
}


DEVICE_IMAGE_LOAD( nascom1_cassette )
{
	nascom1_state *state = image.device().machine->driver_data<nascom1_state>();
	state->tape_size = image.length();
	state->tape_image = (UINT8*)image.ptr();
	if (!state->tape_image)
		return IMAGE_INIT_FAIL;

	state->tape_index = 0;
	return IMAGE_INIT_PASS;
}


DEVICE_IMAGE_UNLOAD( nascom1_cassette )
{
	nascom1_state *state = image.device().machine->driver_data<nascom1_state>();
	state->tape_image = NULL;
	state->tape_size = state->tape_index = 0;
}



/*************************************
 *
 *  Snapshots
 *
 *  ASCII .nas format
 *
 *************************************/

SNAPSHOT_LOAD( nascom1 )
{
	UINT8 line[35];

	while (image.fread( &line, sizeof(line)) == sizeof(line))
	{
		int addr, b0, b1, b2, b3, b4, b5, b6, b7, dummy;

		if (sscanf((char *)line, "%x %x %x %x %x %x %x %x %x %x\010\010\n",
			&addr, &b0, &b1, &b2, &b3, &b4, &b5, &b6, &b7, &dummy) == 10)
		{
			image.device().machine->device("maincpu")->memory().space(AS_PROGRAM)->write_byte(addr++, b0);
			image.device().machine->device("maincpu")->memory().space(AS_PROGRAM)->write_byte(addr++, b1);
			image.device().machine->device("maincpu")->memory().space(AS_PROGRAM)->write_byte(addr++, b2);
			image.device().machine->device("maincpu")->memory().space(AS_PROGRAM)->write_byte(addr++, b3);
			image.device().machine->device("maincpu")->memory().space(AS_PROGRAM)->write_byte(addr++, b4);
			image.device().machine->device("maincpu")->memory().space(AS_PROGRAM)->write_byte(addr++, b5);
			image.device().machine->device("maincpu")->memory().space(AS_PROGRAM)->write_byte(addr++, b6);
			image.device().machine->device("maincpu")->memory().space(AS_PROGRAM)->write_byte(addr++, b7);
		}
	}

	return IMAGE_INIT_PASS;
}



/*************************************
 *
 *  Initialization
 *
 *************************************/

MACHINE_RESET( nascom1 )
{
	nascom1_state *state = machine->driver_data<nascom1_state>();
	state->hd6402 = machine->device("hd6402");

	/* Set up hd6402 pins */
	ay31015_set_input_pin( state->hd6402, AY31015_SWE, 1 );

	ay31015_set_input_pin( state->hd6402, AY31015_CS, 0 );
	ay31015_set_input_pin( state->hd6402, AY31015_NP, 1 );
	ay31015_set_input_pin( state->hd6402, AY31015_NB1, 1 );
	ay31015_set_input_pin( state->hd6402, AY31015_NB2, 1 );
	ay31015_set_input_pin( state->hd6402, AY31015_EPS, 1 );
	ay31015_set_input_pin( state->hd6402, AY31015_TSB, 1 );
	ay31015_set_input_pin( state->hd6402, AY31015_CS, 1 );
}

DRIVER_INIT( nascom1 )
{
	switch (ram_get_size(machine->device(RAM_TAG)))
	{
	case 1 * 1024:
		memory_nop_readwrite(machine->device("maincpu")->memory().space(AS_PROGRAM),
			0x1400, 0x9000, 0, 0);
		break;

	case 16 * 1024:
		memory_install_readwrite_bank(machine->device("maincpu")->memory().space(AS_PROGRAM),
			0x1400, 0x4fff, 0, 0, "bank1");
		memory_nop_readwrite(machine->device("maincpu")->memory().space(AS_PROGRAM),
			0x5000, 0xafff, 0, 0);
		memory_set_bankptr(machine, "bank1", ram_get_ptr(machine->device(RAM_TAG)));
		break;

	case 32 * 1024:
		memory_install_readwrite_bank(machine->device("maincpu")->memory().space(AS_PROGRAM),
			0x1400, 0x8fff, 0, 0, "bank1");
		memory_nop_readwrite(machine->device("maincpu")->memory().space(AS_PROGRAM),
			0x9000, 0xafff, 0, 0);
		memory_set_bankptr(machine, "bank1", ram_get_ptr(machine->device(RAM_TAG)));
		break;

	case 40 * 1024:
		memory_install_readwrite_bank(machine->device("maincpu")->memory().space(AS_PROGRAM),
			0x1400, 0xafff, 0, 0, "bank1");
		memory_set_bankptr(machine, "bank1", ram_get_ptr(machine->device(RAM_TAG)));
		break;
	}
}
