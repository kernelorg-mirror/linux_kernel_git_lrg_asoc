/*
 * Descriptor structures that align with ACPI spec for audio (by Rafal/Vinod/Pierre).
 * They use ASoC values though for enums, it's upto client driver to do any conversion
 * for ambiguous values.
 */

/*
 * ACPI audio descriptors supported by ALSA/ASoC
 */

/*
 *  Supported DAI types
 */
#define SND_DESC_DAI_TYPE_HDA		0
#define SND_DESC_DAI_TYPE_RESERVED1	1
#define SND_DESC_DAI_TYPE_PDM		2
#define SND_DESC_DAI_TYPE_PCM		3
#define SND_DESC_DAI_TYPE_SLIMBUS	4
#define SND_DESC_DAI_TYPE_RESERVED2	5
#define SND_DESC_DAI_TYPE_AC97		6

/*
 * Supported DAI direction.
 * Most DAIs are bidirectional, but some can only do 1 direction.
 */
#define SND_DESC_DAI_DIR_PLAYBACK	0
#define SND_DESC_DAI_DIR_CAPTURE	1
#define SND_DESC_DAI_DIR_BIDIRECTIONAL	2

/*
 * DAI Clock master with respect to the ???
 * TODO: clarify if master is wrt to host or codec side
 * TODO: are we FRAME or BCLK master - some codecs can be mixed master/slave
 */

#define SND_DESC_DAI_CLK_MASTER	0
#define SND_DESC_DAI_CLK_SLAVE	1

/*
 * DAI hardware protocols
 */
#define SND_DESC_DAI_PROT_I2S	0
#define SND_DESC_DAI_PROT_TDM 	1
#define SND_DESC_DAI_PROT_PCM	2
#define SND_DESC_DAI_PROT_PDM	3
#define SND_DESC_DAI_PROT_PCMD	4 /* PCM MSB delayed by 1 BCLK after FRAME */

/*
 * DAI clock polarity. i.e. is signal active high or low.
 * TODO: are we FRAME or BCLK polarity or both ??
 */
#define SND_DESC_DAI_POL_LOW	0
#define SND_DESC_DAI_POL_HIGH	1

/*
 * Generic String Descriptor
 * This descriptor can be used to hold general purpose data that does not easily
 * fit into the other descriptors
 * TODO: Will the text format be driver specific or will we have a standard syntax ??
 */
struct snd_soc_desc_str {
	u32 	capabilities_size;
	u8	*capabilities;
}__attribute__((packed, aligned(1)));


/*
 * PCM stream link parameters descriptor
 */
struct snd_soc_desc_pcm_params {
	u16	format_tag;		// what are values ??
	u16	channels;		/* number of channels */
	u32	sample_per_second;	/* sample rate */
	u32	byte_per_second;	// TODO: is this required since we have bits per sample and srate ??
	u16	block_allign;		// TODO ??
	u16	bits_per_sample;
	u16	size;			// TODO: what size ??
	u16	valid_bit_per_sample;
	u32	channel_mask;
	u8	sub_format[16];
}__attribute__((packed, aligned(1)));	

/*
 * PCM stream link configuration descriptor
 */
struct snd_soc_desc_pcm_config {
	struct snd_soc_desc_pcm_params	params;
	struct snd_soc_desc_str		string;
}__attribute__((packed, aligned(1)));


/*
 * PCM stream links.
 */
struct snd_soc_desc_pcm_configs {
	u8	num_configs;
	u8	reserved[3]; // TODO: added this to give alignment
	struct snd_soc_desc_pcm_config config[0];
}__attribute__((packed, aligned(1)));




struct nhlt_endpoint_descriptor {
	u32	endpoint_descriptor_length;
	u16	deviceId;
        u8	linktype; /* enum linktypes */
	u8	virtualbusid;
	u8	direction; /* enum directions */
	struct specific_config	endpoint_config;
	struct formats_config	format_configs;
}__attribute__((packed, aligned(1)));


/*
 * HW DAI Link Config
 */
struct snd_soc_desc_dai_config {
	u8	link_config_name[16];
	u8	codec_port[4];
	u8	clock_mode; /* enum mode */
	u8	frame_mode; /* enum mode */
	u8	protocol;   /* enum protocols */
	u8	frame_polarity; /* enum polarity */
	u8	reserved[3]; // TODO : changed from 2 to 3 to align
	u32	frame_width;
	u32	frame_rate;
	u8	data_polarity; /* enum polarity */
	u8	tdm_slots;
	u8	bit_per_slots;
	u8	start_delay;
	u32	active_tx_slots;
	u32	active_rx_slots;
}__attribute__((packed, aligned(1)));

/*
 * Multiple HW DAI lInkc configs.
 */
struct snd_soc_desc_dai_configs {
	u32	num_configs;
	struct	snd_soc_desc_dai_config	config[0];
}__attribute__((packed, aligned(1)));


struct clt_link_descriptor {
	u32	link_descriptor_length;
	u8	linktype; /* enum linktypes */
	u8 	virtual_bus_id;
	struct snd_soc_desc_dai_configs	capabilities;
}__attribute__((packed, aligned(1)));


/* just an example for pin */
struct snd_soc_descriptor_pin {
        ....
};

/* more descriptor structures here */


/* descriptor tuple - shall we just make label/value char arrays
* if we have fixed sizes in ACPI table ?? */
struct snd_soc_descriptor_tuple {
        const char *label;
        const char *value;
};


/* client component driver API - called by codec, platform drivers */

/* lets use the new component structure for handle, if it's not ready upstream 
 * we can help or use existing codec, platform varients */

/* we have snd_soc_descriptor_add_() functions for each descriptor structure */

int snd_soc_descriptor_add_dai(struct snd_soc_component *c,
        struct snd_soc_descriptor_dai *dai);

/*.....more client APIs here */

int snd_soc_descriptor_add_pin(struct snd_soc_component *c,
        struct snd_soc_descriptor_pin *pin);

/* general purpose, covers anything */
int snd_soc_descriptor_add_tuple(struct snd_soc_component *c,
        struct snd_soc_descriptor_tuple *tuple);


/* machine driver API - one call for each descriptor type */

int snd_soc_descriptor_get_dai_link(struct snd_soc_card *card, int index,
        struct snd_soc_descriptor_dai_link **link);

int snd_soc_descriptor_get_tuple(struct snd_soc_card *card, const char *label,
        const char **value);

/*..... more machine driver APIs here */


/* core API - used by core to register machines */

/* this structure can be used to define a custom machine driver if one is needed
 * otherwise a deafult machine is used - maybe use componnent instead of
 * codec, platform paradigms */

struct snd_soc_card_descriptor {
        const char *dmi_name; /* the DMI machine name read from ACPI */
	const char *machine_drv; /* optional mach driver to invoke */
        const char *component[]; /* NULL terminated list of components */
};

/* convenience constructor for machines */
#define SND_SOC_MACH_DESC(dname, dmachine, ...) \
	{.dmi_name = dname, .machine_drv = dmachine, .components = __VA_ARGS__)

