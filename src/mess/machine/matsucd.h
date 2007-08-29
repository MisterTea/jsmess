/***************************************************************************

	Matsushita/Panasonic CDR521/522 type CDROM drive emulation

***************************************************************************/

/* initialization */
extern void matsucd_init( void );

/* signaling */
extern void matsucd_enable_w( int level );	/* /ENABLE pin */
extern void matsucd_cmd_w( int level );		/* /CMD pin */
extern int matsucd_stch_r( void );			/* /STCH pin */
extern int matsucd_sten_r( void );			/* /STEN pin */
extern int matsucd_scor_r( void );			/* /SCOR pin */

/* callback for signal changes */
extern void matsucd_set_status_enabled_callback( void (*sten_cb)( int level ) );
extern void matsucd_set_status_changed_callback( void (*stch_cb)( int level ) );
extern void matsucd_set_subcode_ready_callback( void (*scor_cb)( int level ) );

/* data transfer routines */
extern void matsucd_read_next_block( void );
extern int matsucd_get_next_byte( UINT8 *data );

/* main command interface */
extern void matsucd_command_w( UINT8 data );
extern UINT8 matsucd_response_r( void );
