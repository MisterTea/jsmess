#ifndef __INCLUDES_ELF__
#define __INCLUDES_ELF__

#define SCREEN_TAG		"screen"
#define CDP1802_TAG		"a6"
#define CDP1861_TAG		"a14"
#define MM74C923_TAG	"a10"
#define DM9368_L_TAG	"a12"
#define DM9368_H_TAG	"a8"
#define CASSETTE_TAG	"cassette"

class elf2_state : public driver_device
{
public:
	elf2_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

	/* display state */
	int cdp1861_efx;				/* EFx */
	UINT8 data;

	/* devices */
	running_device *cdp1861;
	running_device *mm74c923;
	running_device *dm9368_l;
	running_device *dm9368_h;
	running_device *cassette;
};

#endif
