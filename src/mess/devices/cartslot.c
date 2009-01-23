/**********************************************************************

    cartslot.c

	Cartridge device

**********************************************************************/

#include <ctype.h>
#include "driver.h"
#include "cartslot.h"


/***************************************************************************
    TYPE DEFINITIONS
***************************************************************************/

typedef struct _cartslot_t cartslot_t;
struct _cartslot_t
{
	char dummy;
};


/***************************************************************************
    INLINE FUNCTIONS
***************************************************************************/

INLINE cartslot_t *get_token(const device_config *device)
{
	assert(device != NULL);
	assert(device->type == CARTSLOT);
	return (cartslot_t *) device->token;
}


INLINE const cartslot_config *get_config(const device_config *device)
{
	assert(device != NULL);
	assert(device->type == CARTSLOT);
	return (const cartslot_config *) device->inline_config;
}


/***************************************************************************
    IMPLEMENTATION
***************************************************************************/

/*-------------------------------------------------
    load_cartridge
-------------------------------------------------*/

static int load_cartridge(running_machine *machine, const rom_entry *romrgn, const rom_entry *roment, const device_config *image)
{
	const char *region;
	const char *type;
	UINT32 flags;
	offs_t offset, length, read_length, pos = 0, len;
	UINT8 *ptr;
	UINT8 clear_val;
	int datawidth, littleendian, i, j;
	const device_config *cpu;

	region = ROMREGION_GETTAG(romrgn);
	offset = ROM_GETOFFSET(roment);
	length = ROM_GETLENGTH(roment);
	flags = ROM_GETFLAGS(roment);
	ptr = ((UINT8 *) memory_region(machine, region)) + offset;

	if (image)
	{
		/* must this be full size */
		if (flags & ROM_FULLSIZE)
		{
			if (image_length(image) != length)
				return INIT_FAIL;
		}

		/* read the ROM */
		pos = read_length = image_fread(image, ptr, length);

		/* do we need to mirror the ROM? */
		if (flags & ROM_MIRROR)
		{
			while(pos < length)
			{
				len = MIN(read_length, length - pos);
				memcpy(ptr + pos, ptr, len);
				pos += len;
			}
		}

		/* postprocess this region */
		type = ROMREGION_GETTAG(romrgn);
		littleendian = ROMREGION_ISLITTLEENDIAN(romrgn);
		datawidth = ROMREGION_GETWIDTH(romrgn) / 8;

		/* if the region is inverted, do that now */
		cpu = cputag_get_cpu(machine, type);
		if (cpu != NULL)
		{
			datawidth = cpu_get_databus_width(cpu, ADDRESS_SPACE_PROGRAM) / 8;
			littleendian = (cpu_get_endianness(cpu) == ENDIANNESS_LITTLE);
		}

		/* swap the endianness if we need to */
#ifdef LSB_FIRST
		if (datawidth > 1 && !littleendian)
#else
		if (datawidth > 1 && littleendian)
#endif
		{
			for (i = 0; i < length; i += datawidth)
			{
				UINT8 temp[8];
				memcpy(temp, &ptr[i], datawidth);
				for (j = datawidth - 1; j >= 0; j--)
					ptr[i + j] = temp[datawidth - 1 - j];
			}
		}
	}

	/* clear out anything that remains */
	if (!(flags & ROM_NOCLEAR))
	{
		clear_val = (flags & ROM_FILL_FF) ? 0xFF : 0x00;
		memset(ptr + pos, clear_val, length - pos);
	}
	return INIT_PASS;
}



/*-------------------------------------------------
    process_cartridge
-------------------------------------------------*/

static int process_cartridge(const device_config *image, const device_config *file)
{
	const rom_source *source;
	const rom_entry *romrgn, *roment;
	int result;

	for (source = rom_first_source(image->machine->gamedrv, image->machine->config); source != NULL; source = rom_next_source(image->machine->gamedrv, image->machine->config, source))
	{
		for (romrgn = rom_first_region(image->machine->gamedrv, source); romrgn != NULL; romrgn = rom_next_region(romrgn))
		{
			roment = romrgn + 1;
			while(!ROMENTRY_ISREGIONEND(roment))
			{
				if (ROMENTRY_GETTYPE(roment) == ROMENTRYTYPE_CARTRIDGE)
				{					
					if (strcmp(roment->_hashdata,image->tag)==0)
					{						
						result = load_cartridge(image->machine, romrgn, roment, file);
						if (!result)
							return result;
					}
				}
				roment++;
			}
		}
	}
	return INIT_PASS;
}


/*-------------------------------------------------
    DEVICE_START( cartslot )
-------------------------------------------------*/

static DEVICE_START( cartslot )
{
	const cartslot_config *config = get_config(device);

	/* if this cartridge has a custom DEVICE_START, use it */
	if (config->device_start != NULL)
		return (*config->device_start)(device);
	
	process_cartridge(device, NULL);
}


/*-------------------------------------------------
    DEVICE_IMAGE_LOAD( cartslot )
-------------------------------------------------*/

static DEVICE_IMAGE_LOAD( cartslot )
{	
	const cartslot_config *config = get_config(image);

	/* if this cartridge has a custom DEVICE_IMAGE_LOAD, use it */
	if (config->device_load != NULL)
		return (*config->device_load)(image);

	return process_cartridge(image, image);
}


/*-------------------------------------------------
    DEVICE_IMAGE_UNLOAD( cartslot )
-------------------------------------------------*/

static DEVICE_IMAGE_UNLOAD( cartslot )
{
	const cartslot_config *config = get_config(image);

	/* if this cartridge has a custom DEVICE_IMAGE_UNLOAD, use it */
	if (config->device_unload != NULL)
	{
		(*config->device_unload)(image);
		return;
	}

	process_cartridge(image, NULL);
}


/*-------------------------------------------------
    DEVICE_SET_INFO( cartslot )
-------------------------------------------------*/

static DEVICE_SET_INFO( cartslot )
{
	/* appease compiler */
	get_token(device);

	switch (state)
	{
		/* no parameters to set */
	}
}


/*-------------------------------------------------
    DEVICE_GET_INFO( cartslot )
-------------------------------------------------*/

DEVICE_GET_INFO( cartslot )
{	
	switch(state)
	{
		/* --- the following bits of info are returned as 64-bit signed integers --- */
		case DEVINFO_INT_TOKEN_BYTES:				info->i = sizeof(cartslot_t); break;
		case DEVINFO_INT_INLINE_CONFIG_BYTES:		info->i = sizeof(cartslot_config); break;
		case DEVINFO_INT_CLASS:						info->i = DEVICE_CLASS_PERIPHERAL; break;
		case DEVINFO_INT_IMAGE_TYPE:				info->i = IO_CARTSLOT; break;
		case DEVINFO_INT_IMAGE_READABLE:			info->i = 1; break;
		case DEVINFO_INT_IMAGE_WRITEABLE:			info->i = 0; break;
		case DEVINFO_INT_IMAGE_CREATABLE:			info->i = 0; break;
		case DEVINFO_INT_IMAGE_RESET_ON_LOAD:		info->i = 1; break;
		case DEVINFO_INT_IMAGE_MUST_BE_LOADED:		if ( device && device->inline_config) {
														info->i = get_config(device)->must_be_loaded; 
													} else {
														info->i = 0; 
													}
													break;

		/* --- the following bits of info are returned as pointers to functions --- */
		case DEVINFO_FCT_SET_INFO:					info->set_info = DEVICE_SET_INFO_NAME(cartslot);		break;
		case DEVINFO_FCT_START:						info->start = DEVICE_START_NAME(cartslot);				break;
		case DEVINFO_FCT_IMAGE_LOAD:				info->f = (genf *) DEVICE_IMAGE_LOAD_NAME(cartslot);	break; 
		case DEVINFO_FCT_IMAGE_UNLOAD:				info->f = (genf *) DEVICE_IMAGE_UNLOAD_NAME(cartslot);	break; 
		case DEVINFO_FCT_IMAGE_PARTIAL_HASH:		if ( device && device->inline_config && get_config(device)->device_partialhash) {
														info->f = (genf *) get_config(device)->device_partialhash; 
													} else {
														info->f = NULL; 
													}
													break;			

		/* --- the following bits of info are returned as NULL-terminated strings --- */
		case DEVINFO_STR_NAME:						strcpy(info->s, "Cartslot"); break;
		case DEVINFO_STR_FAMILY:					strcpy(info->s, "Cartslot"); break;
		case DEVINFO_STR_SOURCE_FILE:				strcpy(info->s, __FILE__); break;
		case DEVINFO_STR_IMAGE_FILE_EXTENSIONS:
			if ( device && device->inline_config && get_config(device)->extensions )
			{
				strcpy(info->s, get_config(device)->extensions);
			}
			else
			{
				strcpy(info->s, "bin");
			}
			break;
	}
}
