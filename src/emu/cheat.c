/*********************************************************************

    cheat.c

    MAME cheat system.

    Copyright Nicola Salmoria and the MAME Team.
    Visit http://mamedev.org for licensing and usage restrictions.

**********************************************************************

    Cheat XML format:

    <mamecheat version="1">
        <cheat desc="blah">
           <parameter variable="varname(param)" min="minval(0)" max="maxval(numitems)" step="stepval(1)">
              <item value="itemval(previtemval|minval+stepval)">text</item>
              ...
           </parameter>
           <script state="on|off|run|change(run)">
              <action condition="condexpr(1)">expression</action>
              ...
              <output condition="condexpr(1)" format="format(required)" line="line(0)" align="left|center|right(left)">
                 <argument count="count(1)">expression</argument>
              </output>
              ...
           </script>
           ...
        </cheat>
        ...
    </mamecheat>

*********************************************************************/

#include "driver.h"
#include "xmlfile.h"
#include "ui.h"
#include "debug/debugcpu.h"
#include "debug/express.h"



/***************************************************************************
    MACROS
***************************************************************************/




/***************************************************************************
    CONSTANTS
***************************************************************************/

#define CHEAT_VERSION			1

#define DEFAULT_TEMP_VARIABLES	10

enum _script_state
{
	SCRIPT_STATE_OFF = 0,
	SCRIPT_STATE_ON,
	SCRIPT_STATE_RUN,
	SCRIPT_STATE_CHANGE,
	SCRIPT_STATE_COUNT
};
typedef enum _script_state script_state;



/***************************************************************************
    TYPE DEFINITIONS
***************************************************************************/

/* a single item (string + value) for a cheat parameter */
typedef struct _parameter_item parameter_item;
struct _parameter_item
{
	parameter_item *	next;							/* next item in list */
	astring *			text;							/* name of the item */
	UINT64				value;							/* value of the item */
};


/* a parameter for a cheat, which can be set in the UI */
typedef struct _cheat_parameter cheat_parameter;
struct _cheat_parameter
{
	astring *			name;							/* name of the parameter */
	UINT64				minval;							/* minimum value */
	UINT64				maxval;							/* maximum value */
	UINT64				stepval;						/* step value */
	UINT64				value;							/* live value of the parameter */
	parameter_item *	itemlist;						/* list of items */
};


/* a single argument for an output display */
typedef struct _output_argument output_argument;
struct _output_argument
{
	output_argument *	next;							/* link to next argument */
	parsed_expression *	expression;						/* expression for argument */
	UINT64				count;							/* number of repetitions */
};


/* a single entry within a script, either an expression to execute or a string to output */
typedef struct _script_entry script_entry;
struct _script_entry
{
	script_entry *		next;							/* link to next entry */
	parsed_expression *	condition;						/* condition under which this is executed */
	parsed_expression *	expression;						/* expression to execute */
	astring *			format;							/* string format to print */
	output_argument *	arglist;						/* list of arguments */
	INT8				line;							/* which line to print on */
	UINT8				justify;						/* justification when printing */
};


/* a script entry, specifying which state to execute under */
typedef struct _cheat_script cheat_script;
struct _cheat_script
{
	script_entry *		entrylist;						/* list of actions to perform */
	script_state		state;							/* which state this script is for */
};


/* a single cheat */
typedef struct _cheat_entry cheat_entry;
struct _cheat_entry
{
	cheat_entry *		next;							/* next cheat entry */
	astring *			description;					/* string description/menu title */
	cheat_parameter *	parameter;						/* parameter */
	cheat_script *		script[SCRIPT_STATE_COUNT];		/* up to 1 script for each state */
	symbol_table *		symbols;						/* symbol table for this cheat */
	UINT32				numtemp;						/* number of temporary variables */
	UINT64				tempvar[1];						/* value of the temporary variables */
};


/* private machine-global data */
struct _cheat_private
{
	cheat_entry *		cheatlist;						/* cheat list */
};



/***************************************************************************
    FUNCTION PROTOTYPES
***************************************************************************/

static void cheat_exit(running_machine *machine);

static cheat_entry *cheat_list_load(const char *filename);
static int cheat_list_save(const char *filename, const cheat_entry *cheatlist);
static void cheat_list_free(cheat_entry *cheat);
static cheat_entry *cheat_entry_load(const char *filename, xml_data_node *cheatnode);
static void cheat_entry_save(mame_file *cheatfile, const cheat_entry *cheat);
static void cheat_entry_free(cheat_entry *cheat);
static cheat_parameter *cheat_parameter_load(const char *filename, xml_data_node *paramnode);
static void cheat_parameter_save(mame_file *cheatfile, const cheat_parameter *param);
static void cheat_parameter_free(cheat_parameter *param);
static cheat_script *cheat_script_load(const char *filename, xml_data_node *scriptnode, cheat_entry *cheat);
static void cheat_script_save(mame_file *cheatfile, const cheat_script *script);
static void cheat_script_free(cheat_script *script);
static script_entry *script_entry_load(const char *filename, xml_data_node *entrynode, cheat_entry *cheat, int isaction);
static void script_entry_save(mame_file *cheatfile, const script_entry *entry);
static void script_entry_free(script_entry *entry);

static astring *quote_astring(astring *string, int isattribute);
static int validate_format(const char *filename, int line, const script_entry *entry);
static UINT64 cheat_variable_get(void *ref);
static void cheat_variable_set(void *ref, UINT64 value);



/***************************************************************************
    INLINE FUNCTIONS
***************************************************************************/



/***************************************************************************
    SYSTEM INTERACTION
***************************************************************************/

/*-------------------------------------------------
    cheat_init - initialize the cheat engine,
    loading the cheat file
-------------------------------------------------*/

void cheat_init(running_machine *machine)
{
	cheat_private *cheatinfo;

	/* request a callback */
	add_exit_callback(machine, cheat_exit);

	/* allocate memory */
	cheatinfo = auto_malloc(sizeof(*cheatinfo));
	memset(cheatinfo, 0, sizeof(*cheatinfo));
	machine->cheat_data = cheatinfo;

	/* load the cheat file */
	cheatinfo->cheatlist = cheat_list_load(machine->basename);

	/* temporary: save the file back out as output.xml for comparison */
	if (cheatinfo->cheatlist != NULL)
		cheat_list_save("output", cheatinfo->cheatlist);
}


/*-------------------------------------------------
    cheat_exit - clean up on the way out
-------------------------------------------------*/

static void cheat_exit(running_machine *machine)
{
	cheat_private *cheatinfo = machine->cheat_data;

	/* free the list of cheats */
	if (cheatinfo->cheatlist != NULL)
		cheat_list_free(cheatinfo->cheatlist);
}



/***************************************************************************
    CHEAT FILE ACCESS
***************************************************************************/

/*-------------------------------------------------
    cheat_list_load - load a cheat file into
    memory and create the cheat entry list
-------------------------------------------------*/

static cheat_entry *cheat_list_load(const char *filename)
{
	xml_data_node *rootnode, *mamecheatnode, *cheatnode;
	cheat_entry *cheatlist = NULL;
	cheat_entry **cheattailptr;
	xml_parse_options options;
	xml_parse_error error;
	mame_file *cheatfile;
	file_error filerr;
	astring *fname;
	int version;

	/* open the file with the proper name */
	fname = astring_assemble_2(astring_alloc(), filename, ".xml");
	filerr = mame_fopen(SEARCHPATH_CHEAT, astring_c(fname), OPEN_FLAG_READ, &cheatfile);
	astring_free(fname);

	/* if that failed, return nothing */
	if (filerr != FILERR_NONE)
		return NULL;

	/* read the XML file into internal data structures */
	memset(&options, 0, sizeof(options));
	options.error = &error;
	rootnode = xml_file_read(mame_core_file(cheatfile), &options);
	mame_fclose(cheatfile);

	/* if unable to parse the file, just bail */
	if (rootnode == NULL)
	{
		mame_printf_error("%s.xml(%d): error parsing XML (%s)\n", filename, error.error_line, error.error_message);
		return NULL;
	}

	/* find the layout node */
	mamecheatnode = xml_get_sibling(rootnode->child, "mamecheat");
	if (mamecheatnode == NULL)
	{
		mame_printf_error("%s.xml: missing mamecheatnode node", filename);
		goto error;
	}

	/* validate the config data version */
	version = xml_get_attribute_int(mamecheatnode, "version", 0);
	if (version != CHEAT_VERSION)
	{
		mame_printf_error("%s.xml(%d): Invalid cheat XML file: unsupported version", filename, mamecheatnode->line);
		goto error;
	}

	/* parse all the elements */
	cheatlist = NULL;
	cheattailptr = &cheatlist;
	for (cheatnode = xml_get_sibling(mamecheatnode->child, "cheat"); cheatnode != NULL; cheatnode = xml_get_sibling(cheatnode->next, "cheat"))
	{
		/* load this entry */
		cheat_entry *curcheat = cheat_entry_load(filename, cheatnode);
		if (curcheat == NULL)
			goto error;

		/* add to the end of the list */
		*cheattailptr = curcheat;
		cheattailptr = &curcheat->next;
	}

	/* free the file and exit */
	xml_file_free(rootnode);
	return cheatlist;

error:
	cheat_list_free(cheatlist);
	xml_file_free(rootnode);
	return NULL;
}


/*-------------------------------------------------
    cheat_list_save - save a cheat file from
    memory to the given filename
-------------------------------------------------*/

static int cheat_list_save(const char *filename, const cheat_entry *cheatlist)
{
	mame_file *cheatfile;
	file_error filerr;
	astring *fname;

	/* open the file with the proper name */
	fname = astring_assemble_2(astring_alloc(), filename, ".xml");
	filerr = mame_fopen(SEARCHPATH_CHEAT, astring_c(fname), OPEN_FLAG_WRITE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_PATHS, &cheatfile);
	astring_free(fname);

	/* if that failed, return nothing */
	if (filerr != FILERR_NONE)
		return FALSE;

	/* output the outer layers */
	mame_fprintf(cheatfile, "<?xml version=\"1.0\"?>\n");
	mame_fprintf(cheatfile, "<!-- This file is autogenerated; comments and unknown tags will be stripped -->\n");
	mame_fprintf(cheatfile, "<mamecheat version=\"%d\">\n", CHEAT_VERSION);

	/* iterate over cheats in the list and save them */
	for ( ; cheatlist != NULL; cheatlist = cheatlist->next)
		cheat_entry_save(cheatfile, cheatlist);

	/* close out the file */
	mame_fprintf(cheatfile, "</mamecheat>\n");
	mame_fclose(cheatfile);
	return TRUE;
}


/*-------------------------------------------------
    cheat_list_free - free a list of cheats
-------------------------------------------------*/

static void cheat_list_free(cheat_entry *cheat)
{
	while (cheat != NULL)
	{
		cheat_entry *entry = cheat;
		cheat = entry->next;
		cheat_entry_free(entry);
	}
}


/*-------------------------------------------------
    cheat_entry_load - load a single cheat
    entry and create the underlying data
    structures
-------------------------------------------------*/

static cheat_entry *cheat_entry_load(const char *filename, xml_data_node *cheatnode)
{
	xml_data_node *paramnode, *scriptnode;
	const char *description;
	int tempcount, curtemp;
	cheat_entry *cheat;

	/* pull the variable count out ahead of things */
	tempcount = xml_get_attribute_int(cheatnode, "tempvariables", DEFAULT_TEMP_VARIABLES);
	if (tempcount < 1)
	{
		mame_printf_error("%s.xml(%d): invalid tempvariables attribute (%d)\n", filename, cheatnode->line, tempcount);
		return NULL;
	}

	/* allocate memory for the cheat */
	cheat = malloc_or_die(sizeof(*cheat) + (tempcount - 1) * sizeof(cheat->tempvar));
	memset(cheat, 0, sizeof(*cheat) + (tempcount - 1) * sizeof(cheat->tempvar));
	cheat->numtemp = tempcount;

	/* get the description */
	description = xml_get_attribute_string(cheatnode, "desc", NULL);
	if (description == NULL || description[0] == 0)
	{
		mame_printf_error("%s.xml(%d): empty or missing desc attribute on cheat\n", filename, cheatnode->line);
		return NULL;
	}
	cheat->description = astring_dupc(description);

	/* create the symbol table */
	cheat->symbols = symtable_alloc(NULL);
	for (curtemp = 0; curtemp < tempcount; curtemp++)
	{
		char tempname[20];
		sprintf(tempname, "temp%d", curtemp);
		symtable_add_register(cheat->symbols, tempname, &cheat->tempvar[curtemp], cheat_variable_get, cheat_variable_set);
	}

	/* read the first parameter node */
	paramnode = xml_get_sibling(cheatnode->child, "parameter");
	if (paramnode != NULL)
	{
		/* load this parameter */
		cheat_parameter *curparam = cheat_parameter_load(filename, paramnode);
		if (curparam == NULL)
			goto error;

		/* set this as the parameter and add the symbol */
		cheat->parameter = curparam;
		symtable_add_register(cheat->symbols, astring_c(curparam->name), &curparam->value, cheat_variable_get, cheat_variable_set);

		/* only one parameter allowed */
		paramnode = xml_get_sibling(paramnode->next, "parameter");
		if (paramnode != NULL)
			mame_printf_warning("%s.xml(%d): only one parameter node allowed; ignoring additional nodes\n", filename, paramnode->line);
	}

	/* read the script nodes */
	for (scriptnode = xml_get_sibling(cheatnode->child, "script"); scriptnode != NULL; scriptnode = xml_get_sibling(scriptnode->next, "script"))
	{
		/* load this entry */
		cheat_script *curscript = cheat_script_load(filename, scriptnode, cheat);
		if (curscript == NULL)
			goto error;

		/* if we have a script already for this slot, it is an error */
		if (cheat->script[curscript->state] != NULL)
			mame_printf_warning("%s.xml(%d): only one script per state allowed; ignoring additional scripts\n", filename, scriptnode->line);

		/* otherwise, fill in the slot */
		else
			cheat->script[curscript->state] = curscript;
	}
	return cheat;

error:
	cheat_entry_free(cheat);
	return NULL;
}


/*-------------------------------------------------
    cheat_entry_save - save a single cheat
    entry
-------------------------------------------------*/

static void cheat_entry_save(mame_file *cheatfile, const cheat_entry *cheat)
{
	script_state state;

	/* output the cheat tag */
	mame_fprintf(cheatfile, "\t<cheat desc=\"%s\"", astring_c(cheat->description));
	if (cheat->numtemp != DEFAULT_TEMP_VARIABLES)
		mame_fprintf(cheatfile, " tempvariables=\"%d\"", cheat->numtemp);
	mame_fprintf(cheatfile, ">\n");

	/* output the parameter, if present */
	if (cheat->parameter != NULL)
		cheat_parameter_save(cheatfile, cheat->parameter);

	/* output the script nodes */
	for (state = SCRIPT_STATE_OFF; state < SCRIPT_STATE_COUNT; state++)
		if (cheat->script[state] != NULL)
			cheat_script_save(cheatfile, cheat->script[state]);

	/* close the cheat tag */
	mame_fprintf(cheatfile, "\t</cheat>\n");
}


/*-------------------------------------------------
    cheat_entry_free - free a single cheat entry
-------------------------------------------------*/

static void cheat_entry_free(cheat_entry *cheat)
{
	script_state state;

	if (cheat->description != NULL)
		astring_free(cheat->description);

	if (cheat->parameter != NULL)
		cheat_parameter_free(cheat->parameter);

	for (state = SCRIPT_STATE_OFF; state < SCRIPT_STATE_COUNT; state++)
		if (cheat->script[state] != NULL)
			cheat_script_free(cheat->script[state]);

	if (cheat->symbols != NULL)
		symtable_free(cheat->symbols);

	free(cheat);
}


/*-------------------------------------------------
    cheat_parameter_load - load a single cheat
    parameter and create the underlying data
    structures
-------------------------------------------------*/

static cheat_parameter *cheat_parameter_load(const char *filename, xml_data_node *paramnode)
{
	parameter_item **itemtailptr;
	xml_data_node *itemnode;
	cheat_parameter *param;

	/* allocate memory for it */
	param = malloc_or_die(sizeof(*param));
	memset(param, 0, sizeof(*param));

	/* read the core attributes */
	param->name = astring_dupc(xml_get_attribute_string(paramnode, "variable", "param"));
	param->minval = xml_get_attribute_int(paramnode, "min", 0);
	param->maxval = xml_get_attribute_int(paramnode, "max", 0);
	param->stepval = xml_get_attribute_int(paramnode, "step", 1);

	/* iterate over items */
	itemtailptr = &param->itemlist;
	for (itemnode = xml_get_sibling(paramnode->child, "item"); itemnode != NULL; itemnode = xml_get_sibling(itemnode->next, "item"))
	{
		parameter_item *curitem;

		/* allocate memory for it */
		curitem = malloc_or_die(sizeof(*curitem));
		memset(curitem, 0, sizeof(*curitem));

		/* check for NULL text */
		if (itemnode->value == NULL || itemnode->value[0] == 0)
		{
			mame_printf_error("%s.xml(%d): item is missing text\n", filename, itemnode->line);
			goto error;
		}
		curitem->text = astring_dupc(itemnode->value);

		/* read the attributes */
		if (xml_get_attribute(itemnode, "value") == NULL)
		{
			mame_printf_error("%s.xml(%d): item is value\n", filename, itemnode->line);
			goto error;
		}
		curitem->value = xml_get_attribute_int(itemnode, "value", 0);

		/* add to the end of the list */
		*itemtailptr = curitem;
		itemtailptr = &curitem->next;
	}
	return param;

error:
	cheat_parameter_free(param);
	return NULL;
}


/*-------------------------------------------------
    cheat_parameter_save - save a single cheat
    parameter
-------------------------------------------------*/

static void cheat_parameter_save(mame_file *cheatfile, const cheat_parameter *param)
{
	/* output the parameter tag */
	mame_fprintf(cheatfile, "\t\t<parameter variable=\"%s\"", astring_c(param->name));

	/* if no items, just output min/max/step */
	if (param->itemlist == NULL)
	{
		if (param->minval != 0)
			mame_fprintf(cheatfile, " min=\"%d\"", (UINT32)param->minval);
		if (param->maxval != 0)
			mame_fprintf(cheatfile, " max=\"%d\"", (UINT32)param->maxval);
		if (param->stepval != 1)
			mame_fprintf(cheatfile, " step=\"%d\"", (UINT32)param->stepval);
		mame_fprintf(cheatfile, "/>\n");
	}

	/* iterate over items */
	else
	{
		const parameter_item *curitem;

		mame_fprintf(cheatfile, ">\n");
		for (curitem = param->itemlist; curitem != NULL; curitem = curitem->next)
			mame_fprintf(cheatfile, "\t\t\t<item value=\"%d\">%s</item>\n", (UINT32)curitem->value, astring_c(curitem->text));
		mame_fprintf(cheatfile, "\t\t</parameter>\n");
	}
}


/*-------------------------------------------------
    cheat_parameter_free - free a single cheat
    parameter
-------------------------------------------------*/

static void cheat_parameter_free(cheat_parameter *param)
{
	if (param->name != NULL)
		astring_free(param->name);

	while (param->itemlist != NULL)
	{
		parameter_item *item = param->itemlist;
		param->itemlist = item->next;

		if (item->text != NULL)
			astring_free(item->text);
		free(item);
	}

	free(param);
}


/*-------------------------------------------------
    cheat_script_load - load a single cheat
    script and create the underlying data
    structures
-------------------------------------------------*/

static cheat_script *cheat_script_load(const char *filename, xml_data_node *scriptnode, cheat_entry *cheat)
{
	script_entry **entrytailptr;
	xml_data_node *entrynode;
	cheat_script *script;
	const char *state;

	/* allocate memory for it */
	script = malloc_or_die(sizeof(*script));
	memset(script, 0, sizeof(*script));

	/* read the core attributes */
	script->state = SCRIPT_STATE_RUN;
	state = xml_get_attribute_string(scriptnode, "state", "run");
	if (strcmp(state, "on") == 0)
		script->state = SCRIPT_STATE_ON;
	else if (strcmp(state, "off") == 0)
		script->state = SCRIPT_STATE_OFF;
	else if (strcmp(state, "change") == 0)
		script->state = SCRIPT_STATE_CHANGE;
	else if (strcmp(state, "run") != 0)
	{
		mame_printf_error("%s.xml(%d): invalid script state '%s'\n", filename, scriptnode->line, state);
		goto error;
	}

	/* iterate over nodes within the script */
	entrytailptr = &script->entrylist;
	for (entrynode = scriptnode->child; entrynode != NULL; entrynode = entrynode->next)
	{
		script_entry *curentry = NULL;

		/* handle action nodes */
		if (strcmp(entrynode->name, "action") == 0)
			curentry = script_entry_load(filename, entrynode, cheat, TRUE);

		/* handle output nodes */
		else if (strcmp(entrynode->name, "output") == 0)
			curentry = script_entry_load(filename, entrynode, cheat, FALSE);

		/* anything else is ignored */
		else
		{
			mame_printf_warning("%s.xml(%d): unknown script item '%s' will be lost if saved\n", filename, curentry->line, entrynode->name);
			goto error;
		}

		if (curentry == NULL)
			goto error;

		/* add to the end of the list */
		*entrytailptr = curentry;
		entrytailptr = &curentry->next;
	}
	return script;

error:
	cheat_script_free(script);
	return NULL;
}


/*-------------------------------------------------
    cheat_script_save - save a single cheat
    script
-------------------------------------------------*/

static void cheat_script_save(mame_file *cheatfile, const cheat_script *script)
{
	const script_entry *entry;

	/* output the script tag */
	mame_fprintf(cheatfile, "\t\t<script");
	switch (script->state)
	{
		case SCRIPT_STATE_OFF:		mame_fprintf(cheatfile, " state=\"off\"");		break;
		case SCRIPT_STATE_ON:		mame_fprintf(cheatfile, " state=\"on\"");		break;
		default:
		case SCRIPT_STATE_RUN:		mame_fprintf(cheatfile, " state=\"run\"");		break;
		case SCRIPT_STATE_CHANGE:	mame_fprintf(cheatfile, " state=\"change\"");	break;
	}
	mame_fprintf(cheatfile, ">\n");

	/* output entries */
	for (entry = script->entrylist; entry != NULL; entry = entry->next)
		script_entry_save(cheatfile, entry);

	/* close the tag */
	mame_fprintf(cheatfile, "\t\t</script>\n");
}


/*-------------------------------------------------
    cheat_script_free - free a single cheat
    script
-------------------------------------------------*/

static void cheat_script_free(cheat_script *script)
{
	while (script->entrylist != NULL)
	{
		script_entry *entry = script->entrylist;
		script->entrylist = entry->next;
		script_entry_free(entry);
	}

	free(script);
}


/*-------------------------------------------------
    script_entry_load - load a single action
    or output create the underlying data
    structures
-------------------------------------------------*/

static script_entry *script_entry_load(const char *filename, xml_data_node *entrynode, cheat_entry *cheat, int isaction)
{
	const char *expression;
	script_entry *entry;
	EXPRERR experr;

	/* allocate memory for it */
	entry = malloc_or_die(sizeof(*entry));
	memset(entry, 0, sizeof(*entry));

	/* read the condition if present */
	expression = xml_get_attribute_string(entrynode, "condition", NULL);
	if (expression != NULL)
	{
		experr = expression_parse(expression, cheat->symbols, &debug_expression_callbacks, &entry->condition);
		if (experr != EXPRERR_NONE)
		{
			mame_printf_error("%s.xml(%d): error parsing cheat expression \"%s\" (%s)\n", filename, entrynode->line, expression, exprerr_to_string(experr));
			goto error;
		}
	}

	/* if this is an action, parse the expression */
	if (isaction)
	{
		expression = entrynode->value;
		if (expression == NULL || expression[0] == 0)
		{
			mame_printf_error("%s.xml(%d): missing expression in action tag\n", filename, entrynode->line);
			goto error;
		}
		experr = expression_parse(expression, cheat->symbols, &debug_expression_callbacks, &entry->expression);
		if (experr != EXPRERR_NONE)
		{
			mame_printf_error("%s.xml(%d): error parsing cheat expression \"%s\" (%s)\n", filename, entrynode->line, expression, exprerr_to_string(experr));
			goto error;
		}
	}

	/* otherwise, parse the attributes and arguments */
	else
	{
		output_argument **argtailptr;
		const char *align, *format;
		xml_data_node *argnode;

		/* extract format */
		format = xml_get_attribute_string(entrynode, "format", NULL);
		if (format == NULL || format[0] == 0)
		{
			mame_printf_error("%s.xml(%d): missing format in output tag\n", filename, entrynode->line);
			goto error;
		}
		entry->format = astring_dupc(format);

		/* extract other attributes */
		entry->line = xml_get_attribute_int(entrynode, "line", 0);
		entry->justify = JUSTIFY_LEFT;
		align = xml_get_attribute_string(entrynode, "align", "left");
		if (strcmp(align, "center") == 0)
			entry->justify = JUSTIFY_CENTER;
		else if (strcmp(align, "right") == 0)
			entry->justify = JUSTIFY_RIGHT;
		else if (strcmp(align, "left") != 0)
		{
			mame_printf_error("%s.xml(%d): invalid alignment '%s' specified\n", filename, entrynode->line, align);
			goto error;
		}

		/* then parse arguments */
		argtailptr = &entry->arglist;
		for (argnode = xml_get_sibling(entrynode->child, "argument"); argnode != NULL; argnode = xml_get_sibling(argnode->next, "argument"))
		{
			output_argument *curarg;

			/* allocate memory for it */
			curarg = malloc_or_die(sizeof(*curarg));
			memset(curarg, 0, sizeof(*curarg));

			/* first extract attributes */
			curarg->count = xml_get_attribute_int(argnode, "count", 1);

			/* read the expression */
			expression = argnode->value;
			if (expression == NULL || expression[0] == 0)
			{
				mame_printf_error("%s.xml(%d): missing expression in argument tag\n", filename, argnode->line);
				goto error;
			}
			experr = expression_parse(expression, cheat->symbols, &debug_expression_callbacks, &curarg->expression);
			if (experr != EXPRERR_NONE)
			{
				mame_printf_error("%s.xml(%d): error parsing cheat expression \"%s\" (%s)\n", filename, argnode->line, expression, exprerr_to_string(experr));
				goto error;
			}

			/* add to the end of the list */
			*argtailptr = curarg;
			argtailptr = &curarg->next;
		}

		/* validate the format against the arguments */
		if (!validate_format(filename, entrynode->line, entry))
			goto error;
	}
	return entry;

error:
	script_entry_free(entry);
	return NULL;
}


/*-------------------------------------------------
    script_entry_save - save a single action
    or output
-------------------------------------------------*/

static void script_entry_save(mame_file *cheatfile, const script_entry *entry)
{
	astring *tempstring = astring_alloc();

	/* output an action */
	if (entry->format == NULL)
	{
		mame_fprintf(cheatfile, "\t\t\t<action");
		if (entry->condition != NULL)
		{
			quote_astring(astring_cpyc(tempstring, expression_original_string(entry->condition)), TRUE);
			mame_fprintf(cheatfile, " condition=\"%s\"", astring_c(tempstring));
		}
		quote_astring(astring_cpyc(tempstring, expression_original_string(entry->expression)), FALSE);
		mame_fprintf(cheatfile, ">%s</action>\n", astring_c(tempstring));
	}

	/* output an output */
	else
	{
		mame_fprintf(cheatfile, "\t\t\t<output format=\"%s\"", astring_c(entry->format));
		if (entry->condition != NULL)
		{
			quote_astring(astring_cpyc(tempstring, expression_original_string(entry->condition)), TRUE);
			mame_fprintf(cheatfile, " condition=\"%s\"", astring_c(tempstring));
		}
		if (entry->line != 0)
			mame_fprintf(cheatfile, " line=\"%d\"", entry->line);
		if (entry->justify == JUSTIFY_CENTER)
			mame_fprintf(cheatfile, " align=\"center\"");
		else if (entry->justify == JUSTIFY_RIGHT)
			mame_fprintf(cheatfile, " align=\"right\"");
		if (entry->arglist == NULL)
			mame_fprintf(cheatfile, " />\n");

		/* output arguments */
		else
		{
			const output_argument *curarg;

			mame_fprintf(cheatfile, ">\n");
			for (curarg = entry->arglist; curarg != NULL; curarg = curarg->next)
			{
				mame_fprintf(cheatfile, "\t\t\t\t<argument");
				if (curarg->count != 1)
					mame_fprintf(cheatfile, " count=\"%d\"", (int)curarg->count);
				quote_astring(astring_cpyc(tempstring, expression_original_string(curarg->expression)), FALSE);
				mame_fprintf(cheatfile, ">%s</argument>\n", astring_c(tempstring));
			}
			mame_fprintf(cheatfile, "\t\t\t</output>\n");
		}
	}

	astring_free(tempstring);
}


/*-------------------------------------------------
    script_entry_free - free a single script
    entry
-------------------------------------------------*/

static void script_entry_free(script_entry *entry)
{
	if (entry->condition != NULL)
		expression_free(entry->condition);
	if (entry->expression != NULL)
		expression_free(entry->expression);
	if (entry->format != NULL)
		astring_free(entry->format);

	while (entry->arglist != NULL)
	{
		output_argument *curarg = entry->arglist;
		entry->arglist = curarg->next;

		if (curarg->expression != NULL)
			expression_free(curarg->expression);
		free(curarg);
	}

	free(entry);
}



/***************************************************************************
    MISC HELPERS
***************************************************************************/

/*-------------------------------------------------
    quote_astring - quote a string so that it
    is valid to embed in an XML document
-------------------------------------------------*/

static astring *quote_astring(astring *string, int isattribute)
{
	astring_replacec(string, 0, "&", "&amp;");
	astring_replacec(string, 0, "<", "&lt;");
	if (isattribute)
		astring_replacec(string, 0, "\"", "&quot;");
	return string;
}


/*-------------------------------------------------
    validate_format - check that a format string
    has the correct number and type of arguments
-------------------------------------------------*/

static int validate_format(const char *filename, int line, const script_entry *entry)
{
	const char *p = astring_c(entry->format);
	const output_argument *curarg;
	int argsprovided;
	int argscounted;

	/* first count arguments */
	argsprovided = 0;
	for (curarg = entry->arglist; curarg != NULL; curarg = curarg->next)
		argsprovided += curarg->count;

	/* now scan the string for valid argument usage */
	p = strchr(p, '%');
	argscounted = 0;
	while (p != NULL)
	{
		/* skip past any valid attributes */
		p++;
		while (strchr("lh0123456789.-+ #", *p) != NULL)
			p++;

		/* look for a valid type */
		if (strchr("cdiouxX", *p) == NULL)
		{
			mame_printf_error("%s.xml(%d): invalid format specification \"%s\"\n", filename, line, astring_c(entry->format));
			return FALSE;
		}
		argscounted++;

		/* look for the next one */
		p = strchr(p, '%');
	}

	/* did we match? */
	if (argscounted < argsprovided)
	{
		mame_printf_error("%s.xml(%d): too many arguments provided (%d) for format \"%s\"\n", filename, line, argsprovided, astring_c(entry->format));
		return FALSE;
	}
	if (argscounted > argsprovided)
	{
		mame_printf_error("%s.xml(%d): not enough arguments provided (%d) for format \"%s\"\n", filename, line, argsprovided, astring_c(entry->format));
		return FALSE;
	}
	return TRUE;
}


/*-------------------------------------------------
    cheat_variable_get - return the value of a
    cheat variable
-------------------------------------------------*/

static UINT64 cheat_variable_get(void *ref)
{
	return *(UINT64 *)ref;
}


/*-------------------------------------------------
    cheat_variable_set - set the value of a
    cheat variable
-------------------------------------------------*/

static void cheat_variable_set(void *ref, UINT64 value)
{
	*(UINT64 *)ref = value;
}

