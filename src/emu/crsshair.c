/***************************************************************************

    crsshair.h

    Crosshair handling.

    Copyright Nicola Salmoria and the MAME Team.
    Visit http://mamedev.org for licensing and usage restrictions.

***************************************************************************/

#include "driver.h"
#include "rendutil.h"



/***************************************************************************
    CONSTANTS
***************************************************************************/

#define CROSSHAIR_RAW_SIZE		100
#define CROSSHAIR_RAW_ROWBYTES	((CROSSHAIR_RAW_SIZE + 7) / 8)


/***************************************************************************
    TYPE DEFINITIONS
***************************************************************************/

typedef struct _crosshair_global crosshair_global;
struct _crosshair_global
{
	UINT8 				used[MAX_PLAYERS];		/* usage per player */
	UINT8 				visible[MAX_PLAYERS];	/* visibility per player */
	bitmap_t *			bitmap[MAX_PLAYERS];	/* bitmap per player */
	render_texture *	texture[MAX_PLAYERS];	/* texture per player */
	const device_config *screen[MAX_PLAYERS];	/* the screen on which this player's crosshair is drawn */
	float 				x[MAX_PLAYERS];			/* current X position */
	float				y[MAX_PLAYERS];			/* current Y position */
	UINT8				fade;					/* color fading factor */
	UINT8 				animation_counter;		/* animation frame index */
};


/***************************************************************************
    GLOBAL VARIABLES
***************************************************************************/

/* global state */
static crosshair_global global;

/* raw bitmap */
static const UINT8 crosshair_raw_top[] =
{
	0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,
	0x00,0x70,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xe0,0x00,
	0x00,0xf8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xf0,0x00,
	0x01,0xf8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xf8,0x00,
	0x03,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0xfc,0x00,
	0x07,0xfe,0x00,0x00,0x00,0x0f,0xfe,0x00,0x00,0x00,0x07,0xfe,0x00,
	0x0f,0xff,0x00,0x00,0x01,0xff,0xff,0xf0,0x00,0x00,0x0f,0xff,0x00,
	0x1f,0xff,0x80,0x00,0x1f,0xff,0xff,0xff,0x00,0x00,0x1f,0xff,0x80,
	0x3f,0xff,0x80,0x00,0xff,0xff,0xff,0xff,0xe0,0x00,0x1f,0xff,0xc0,
	0x7f,0xff,0xc0,0x03,0xff,0xff,0xff,0xff,0xf8,0x00,0x3f,0xff,0xe0,
	0xff,0xff,0xe0,0x07,0xff,0xff,0xff,0xff,0xfc,0x00,0x7f,0xff,0xf0,
	0x7f,0xff,0xf0,0x1f,0xff,0xff,0xff,0xff,0xff,0x00,0xff,0xff,0xe0,
	0x3f,0xff,0xf8,0x7f,0xff,0xff,0xff,0xff,0xff,0xc1,0xff,0xff,0xc0,
	0x0f,0xff,0xf8,0xff,0xff,0xff,0xff,0xff,0xff,0xe1,0xff,0xff,0x00,
	0x07,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xfb,0xff,0xfe,0x00,
	0x03,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xfc,0x00,
	0x01,0xff,0xff,0xff,0xff,0xf0,0x01,0xff,0xff,0xff,0xff,0xf8,0x00,
	0x00,0x7f,0xff,0xff,0xff,0x00,0x00,0x1f,0xff,0xff,0xff,0xe0,0x00,
	0x00,0x3f,0xff,0xff,0xf8,0x00,0x00,0x03,0xff,0xff,0xff,0xc0,0x00,
	0x00,0x1f,0xff,0xff,0xe0,0x00,0x00,0x00,0xff,0xff,0xff,0x80,0x00,
	0x00,0x0f,0xff,0xff,0x80,0x00,0x00,0x00,0x3f,0xff,0xff,0x00,0x00,
	0x00,0x03,0xff,0xfe,0x00,0x00,0x00,0x00,0x0f,0xff,0xfc,0x00,0x00,
	0x00,0x01,0xff,0xfc,0x00,0x00,0x00,0x00,0x07,0xff,0xf8,0x00,0x00,
	0x00,0x03,0xff,0xf8,0x00,0x00,0x00,0x00,0x01,0xff,0xf8,0x00,0x00,
	0x00,0x07,0xff,0xfc,0x00,0x00,0x00,0x00,0x03,0xff,0xfc,0x00,0x00,
	0x00,0x0f,0xff,0xfe,0x00,0x00,0x00,0x00,0x07,0xff,0xfe,0x00,0x00,
	0x00,0x0f,0xff,0xff,0x00,0x00,0x00,0x00,0x0f,0xff,0xfe,0x00,0x00,
	0x00,0x1f,0xff,0xff,0x80,0x00,0x00,0x00,0x1f,0xff,0xff,0x00,0x00,
	0x00,0x1f,0xff,0xff,0x80,0x00,0x00,0x00,0x1f,0xff,0xff,0x00,0x00,
	0x00,0x3f,0xfe,0xff,0xc0,0x00,0x00,0x00,0x3f,0xff,0xff,0x80,0x00,
	0x00,0x7f,0xfc,0x7f,0xe0,0x00,0x00,0x00,0x7f,0xe7,0xff,0xc0,0x00,
	0x00,0x7f,0xf8,0x3f,0xf0,0x00,0x00,0x00,0xff,0xc3,0xff,0xc0,0x00,
	0x00,0xff,0xf8,0x1f,0xf8,0x00,0x00,0x01,0xff,0x83,0xff,0xe0,0x00,
	0x00,0xff,0xf0,0x07,0xf8,0x00,0x00,0x01,0xfe,0x01,0xff,0xe0,0x00,
	0x00,0xff,0xf0,0x03,0xfc,0x00,0x00,0x03,0xfc,0x01,0xff,0xe0,0x00,
	0x01,0xff,0xe0,0x01,0xfe,0x00,0x00,0x07,0xf8,0x00,0xff,0xf0,0x00,
	0x01,0xff,0xe0,0x00,0xff,0x00,0x00,0x0f,0xf0,0x00,0xff,0xf0,0x00,
	0x01,0xff,0xc0,0x00,0x3f,0x80,0x00,0x1f,0xc0,0x00,0x7f,0xf0,0x00,
	0x01,0xff,0xc0,0x00,0x1f,0x80,0x00,0x1f,0x80,0x00,0x7f,0xf0,0x00,
	0x03,0xff,0xc0,0x00,0x0f,0xc0,0x00,0x3f,0x00,0x00,0x7f,0xf8,0x00,
	0x03,0xff,0x80,0x00,0x07,0xe0,0x00,0x7e,0x00,0x00,0x3f,0xf8,0x00,
	0x03,0xff,0x80,0x00,0x01,0xf0,0x00,0xf8,0x00,0x00,0x3f,0xf8,0x00,
	0x03,0xff,0x80,0x00,0x00,0xf8,0x01,0xf0,0x00,0x00,0x3f,0xf8,0x00,
	0x03,0xff,0x80,0x00,0x00,0x78,0x01,0xe0,0x00,0x00,0x3f,0xf8,0x00,
	0x07,0xff,0x00,0x00,0x00,0x3c,0x03,0xc0,0x00,0x00,0x3f,0xfc,0x00,
	0x07,0xff,0x00,0x00,0x00,0x0e,0x07,0x00,0x00,0x00,0x1f,0xfc,0x00,
	0x07,0xff,0x00,0x00,0x00,0x07,0x0e,0x00,0x00,0x00,0x1f,0xfc,0x00,
	0x07,0xff,0x00,0x00,0x00,0x03,0x9c,0x00,0x00,0x00,0x1f,0xfc,0x00,
	0x07,0xff,0x00,0x00,0x00,0x01,0x98,0x00,0x00,0x00,0x1f,0xfc,0x00,
	0x07,0xff,0x00,0x00,0x00,0x00,0x60,0x00,0x00,0x00,0x1f,0xfc,0x00
};

/* per-player colors */
static const rgb_t crosshair_colors[] =
{
	MAKE_RGB(0x40,0x40,0xff),
	MAKE_RGB(0xff,0x40,0x40),
	MAKE_RGB(0x40,0xff,0x40),
	MAKE_RGB(0xff,0xff,0x40),
	MAKE_RGB(0xff,0x40,0xff),
	MAKE_RGB(0x40,0xff,0xff),
	MAKE_RGB(0xff,0xff,0xff)
};


/***************************************************************************
    FUNCTION PROTOTYPES
***************************************************************************/

static void crosshair_exit(running_machine *machine);
static void animate(const device_config *device, int vblank_state);


/***************************************************************************
    CORE IMPLEMENTATION
***************************************************************************/


/*-------------------------------------------------
    create_bitmap - create the rendering
    structures for the given player
-------------------------------------------------*/

static void create_bitmap(int player)
{
	/* if we don't have a bitmap for this player yet */
	if (global.bitmap[player] == NULL)
	{
		int x, y;
		char filename[20];
		rgb_t color = crosshair_colors[player];

		/* first try to load a bitmap for the crosshair */
		sprintf(filename, "cross%d.png", player);
		global.bitmap[player] = render_load_png(NULL, filename, NULL, NULL);

		/* if that didn't work, use the built-in one */
		if (global.bitmap[player] == NULL)
		{
			/* allocate a blank bitmap to start with */
			global.bitmap[player] = bitmap_alloc(CROSSHAIR_RAW_SIZE, CROSSHAIR_RAW_SIZE, BITMAP_FORMAT_ARGB32);
			fillbitmap(global.bitmap[player], MAKE_ARGB(0x00,0xff,0xff,0xff), NULL);

			/* extract the raw source data to it */
			for (y = 0; y < CROSSHAIR_RAW_SIZE / 2; y++)
			{
				/* assume it is mirrored vertically */
				UINT32 *dest0 = BITMAP_ADDR32(global.bitmap[player], y, 0);
				UINT32 *dest1 = BITMAP_ADDR32(global.bitmap[player], CROSSHAIR_RAW_SIZE - 1 - y, 0);

				/* extract to two rows simultaneously */
				for (x = 0; x < CROSSHAIR_RAW_SIZE; x++)
					if ((crosshair_raw_top[y * CROSSHAIR_RAW_ROWBYTES + x / 8] << (x % 8)) & 0x80)
						dest0[x] = dest1[x] = MAKE_ARGB(0xff,0x00,0x00,0x00) | color;
			}
		}

		/* create a texture to reference the bitmap */
		global.texture[player] = render_texture_alloc(render_texture_hq_scale, NULL);
		render_texture_set_bitmap(global.texture[player], global.bitmap[player], NULL, 0, TEXFORMAT_ARGB32);
	}
}


/*-------------------------------------------------
    crosshair_init - initialize the crosshair
    bitmaps and such
-------------------------------------------------*/

void crosshair_init(running_machine *machine)
{
	input_port_entry *ipt;

	/* request a callback upon exiting */
	add_exit_callback(machine, crosshair_exit);

	/* clear all the globals */
	memset(&global, 0, sizeof(global));

	/* determine who needs crosshairs */
	for (ipt = machine->input_ports; ipt->type != IPT_END; ipt++)
		if (ipt->analog.crossaxis != CROSSHAIR_AXIS_NONE)
		{
			int player = ipt->player;

			assert(player < MAX_PLAYERS);

			/* mark as used and visible */
			global.used[player] = TRUE;
			global.visible[player] = TRUE;

			/* for now, use the main screen */
			global.screen[player] = machine->primary_screen;

			create_bitmap(player);
		}

	/* register the animation callback */
	if (machine->primary_screen != NULL)
		video_screen_register_vblank_callback(machine->primary_screen, animate);
}


/*-------------------------------------------------
    crosshair_exit - free memory allocated for
    the crosshairs
-------------------------------------------------*/

static void crosshair_exit(running_machine *machine)
{
	int player;

	/* free bitmaps and textures for each player */
	for (player = 0; player < MAX_PLAYERS; player++)
	{
		if (global.texture[player] != NULL)
			render_texture_free(global.texture[player]);
		global.texture[player] = NULL;

		if (global.bitmap[player] != NULL)
			bitmap_free(global.bitmap[player]);
		global.bitmap[player] = NULL;
	}
}


/*-------------------------------------------------
    crosshair_toggle - toggle crosshair
    visibility
-------------------------------------------------*/

void crosshair_toggle(running_machine *machine)
{
	int player;
	int first_hidden_player = -1;

	/* find the first invisible crosshair */
	for (player = 0; player < MAX_PLAYERS; player++)
		if (global.used[player] && !global.visible[player])
		{
			first_hidden_player = player;
			break;
		}

	/* if all visible, turn all off */
	if (first_hidden_player == -1)
		for (player = 0; player < MAX_PLAYERS; player++)
			global.visible[player] = FALSE;

	/* otherwise, turn on the first one that isn't currently on */
	else
		global.visible[first_hidden_player] = TRUE;
}


/*-------------------------------------------------
    animate - animates the crosshair once a frame
-------------------------------------------------*/

static void animate(const device_config *device, int vblank_state)
{
	input_port_entry *ipt;
	int portnum = -1;

	/* increment animation counter */
	global.animation_counter += 0x04;

	/* compute a fade factor from the current animation value */
	if (global.animation_counter < 0x80)
		global.fade = 0xa0 + (0x60 * ( global.animation_counter & 0x7f) / 0x80);
	else
		global.fade = 0xa0 + (0x60 * (~global.animation_counter & 0x7f) / 0x80);

	/* read all the lightgun values */
	for (ipt = device->machine->input_ports; ipt->type != IPT_END; ipt++)
	{
		/* keep track of the port number */
		if (ipt->type == IPT_PORT)
			portnum++;

		/* compute the values */
		if (ipt->analog.crossaxis != CROSSHAIR_AXIS_NONE)
		{
			float value = (float)(get_crosshair_pos(portnum, ipt->player, ipt->analog.crossaxis) - ipt->analog.min) / (float)(ipt->analog.max - ipt->analog.min);
			if (ipt->analog.crossscale < 0)
				value = -(1.0 - value) * ipt->analog.crossscale;
			else
				value *= ipt->analog.crossscale;
			value += ipt->analog.crossoffset;

			/* switch off the axis */
			switch (ipt->analog.crossaxis)
			{
				case CROSSHAIR_AXIS_X:
					global.x[ipt->player] = value;
					if (ipt->analog.crossaltaxis != 0)
						global.y[ipt->player] = ipt->analog.crossaltaxis;
					break;

				case CROSSHAIR_AXIS_Y:
					global.y[ipt->player] = value;
					if (ipt->analog.crossaltaxis != 0)
						global.x[ipt->player] = ipt->analog.crossaltaxis;
					break;
			}
		}
	}
}


/*-------------------------------------------------
    crosshair_render - render the crosshairs
    for the given screen
-------------------------------------------------*/

void crosshair_render(const device_config *screen)
{
	int player;

	for (player = 0; player < MAX_PLAYERS; player++)
		/* draw if visible and the right screen */
		if (global.visible[player] && (global.screen[player] == screen))
		{
			/* add a quad assuming a 4:3 screen (this is not perfect) */
			render_screen_add_quad(screen,
						global.x[player] - 0.03f, global.y[player] - 0.04f,
						global.x[player] + 0.03f, global.y[player] + 0.04f,
						MAKE_ARGB(0xc0, global.fade, global.fade, global.fade),
						global.texture[player], PRIMFLAG_BLENDMODE(BLENDMODE_ALPHA));
		}
}
