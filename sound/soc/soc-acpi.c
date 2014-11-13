/*
 * snd-acpi.c  --  ACPI audio helper
 *
 * Copyright 2014 Intel Corporation.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */


/*
 * Container for component data.
 * Each component driver can store multiple descriptors. This structure
 * represents one component descriptor.
 */
struct soc_desc_data {
	enum snd_soc_desc_type type;	/* type of descriptor data */
	const void *data;		/* descriptor data */
	struct list_head list;
};

/*
 * Describes every component registered.
 */
struct soc_desc_comp {
	const char *name;
	struct snd_soc_component *c;
	struct list_head list;
	struct list_head data_list; /* list of data */
};

/*
 * Singleton that tracks the state of every registered component.
 */
struct soc_desc_state {
	/* runtime */
	const struct snd_soc_card_descriptor *desc;
	int missing_components;
	bool dmi_scan_done;

	/* components */
	struct list_head component_list; /* list of components */
};

/* static singleton for the moment */
static struct soc_desc_state state_ = {
	.desc = NULL,
	.dmi_scan_done = false;
	.missing_components = 0,
};

/* list of audio configurations we care about */
static const struct snd_soc_card_descriptor machine[] = {
	/* HSW + RT5640 */
	SND_SOC_MACH_DESC("Haswell", "haswell-audio", "INT33C8", "INT33CA", NULL},
	/* BDW + RT286 */
	SND_SOC_MACH_DESC("Broadwell", "broadwell-audio", "INT343A", "INT3438", NULL},
	/* BYT + RT25640 */
	SND_SOC_MACH_DESC("Baytrail", "byt-rt5640", "80860F28", "10EC5640", NULL},
};

static int dmi_config_found(const struct dmi_system_id *d)
{
	dev_info(dev, "found DMI audio config for %s\n", d->ident);
	state_.desc = d->driver_data;
	return 0;
}

/* list of machines we care about */
static const struct dmi_system_id __initconst dmi_table[] = {
{
	.ident = "Intel Harris Beach",
	.matches = {DMI_MATCH(DMI_BOARD_NAME, "Harris Beach SDS")},
	.callback = dmi_config_found,
	.driver_data = &machine[0],
},
{
	.ident = "Intel Wilson Beach",
	.matches = {DMI_MATCH(DMI_BOARD_NAME, "Wilson Beach SDS")},
	.callback = dmi_config_found,
	.driver_data = &machine[1],
},
{
	// TODO: add Asus T100
	.ident = "Intel Baytrail",
	.matches = {DMI_MATCH(DMI_BOARD_NAME, "Baytrail Machine")},
	.callback = dmi_config_found,
	.driver_data = &machine[2],
},
{ }
};

/* match DMI name against descriptor list */
static int match_dmi_name(struct soc_desc_state *state, struct device *dev)
{
	const char *dmi_name;
	int i, count;

	INIT_LIST_HEAD(&state->component_list);

	/* get machine dmi name */
	count = dmi_check_system(dmi_table);
	if (count == 0) {
		/* no match from table so prepare generic machine driver */
		dev_info(dev, "no DMI audio card config found, using generic\n");
	} else {

	}

	state->dmi_scan_done = true;
}

/* initialises state, called by all client calls but run once */
static int init_state(struct soc_desc_state *state, struct device *dev)
{
	int ret;

	/* init dmi scan already done ? */
	if (state->dmi_scan_done)
		return 0;

	/* initialise the state - match name first */
	ret = match_dmi_name(state);
	if (ret < 0)
		return ret;

	return 0;
}

/* get the descriptor component for given asoc component */
static struct soc_desc_comp *soc_comp_get(struct soc_desc_state *state,
	struct snd_soc_component *c)
{
	struct soc_desc_comp *dcomp;

	/* search existing descriptor components for this one */
	//list_for_each_entry(

	/* not found, then create and append */
	if (dcomp == NULL) {
		dcomp = kzalloc(sizeof(*dcomp);
		if (dcomp == NULL)
			return NULL;
		INIT_LIST_HEAD(&dcomp->data_list);
		list_add(&dcomp->list, &state->component_list);
	}

	return dcomp;
}

static void soc_comp_put(struct soc_desc_state *state,
	struct soc_desc_comp *dcomp)
{
	list_del(&dcomp->list);
	kfree(dcomp);
}

/* append data pointer of any type to component descriptor */
static int soc_dcomp_append_data(struct soc_desc_comp *dcomp,
	enum snd_soc_desc_type, void *data)
{
	struct soc_desc_data *data;

	data = kzalloc(sizeof(*data));
	if (data == NULL)
		return -ENOMEM;

	data->type = type;
	data->data = data;
	list_add(&data->list, &dcomp->data_list);
	return 0;
}

/* add new DAI data to the component */
int snd_descriptor_new_dai(struct snd_component *c,
        struct snd_desc_dai_descriptor *dai_desc)
{
	struct soc_desc_comp *dcomp;
	struct snd_desc_dai_descriptor *d;
	int ret;

	/* initialise if not already done so */
	ret = init_state(&state_, c->dev));
	if (ret < 0)
		return ret;

	/* allocate memory for descriptor */
	d = kzalloc(dai_desc->length);
	if (d = NULL)
		return -ENOMEM;

	/* get descriptor componnent */
	dcomp = soc_comp_get(c);
	if (dcomp == NULL) {
		kfree(d);
		return -ENOMEM;
	}

	/* append new data */
	ret = soc_dcomp_append_data(dcomp, SND_SOC_DESC_DAI, (void*)d);
	if (ret < 0) {
		soc_comp_put(&state_, d);
		kfree(d);
	}

	return ret;
}


/* should be called when driver module is removed */
void snd_descriptor_free_dai(struct snd_component *c, int vbus_id);

/* TODO: should we rename to snd_desc_new_nhlt ?? */
int snd_descriptor_new_pcm(struct snd_component *c,
        struct snd_desc_nhlt_endpoint *pcm_desc);

/* should be called when driver module is removed */
void snd_descriptor_free_pcm(struct snd_component *c, int vbus_id);


/* machine driver API - one call for each descriptor type */

int snd_descriptor_get_dai(struct snd_card *card,
        const struct snd_desc_dai_descriptor **dai_desc);

int snd_descriptor_get_pcm(struct snd_card *card,
        const struct snd_desc_nhlt_endpoint **pcm_desc);

/*..... more machine driver APIs here */

