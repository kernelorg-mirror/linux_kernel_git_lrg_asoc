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
 * Types of non-hd audio device on the basis of
 * DAI types.
 */
#define SND_DESC_DEVICE_TYPE_PCM_BT_SIDEBAND	0
#define SND_DESC_DEVICE_TYPE_PCM_MODEM    	1
#define SND_DESC_DEVICE_TYPE_PCM_FM    	2
#define SND_DESC_DEVICE_TYPE_PCM_RESERVED1    	3
#define SND_DESC_DEVICE_TYPE_PCM_ANALOG_CODEC	4
#define SND_DESC_DEVICE_TYPE_PCM_RESERVED2   	5
#define SND_DESC_DEVICE_TYPE_PCM_RESERVED3   	6
#define SND_DESC_DEVICE_TYPE_PCM_RESERVED4   	7
#define SND_DESC_DEVICE_TYPE_PDM		0


/*
 * Supported DAI direction.
 * Most DAIs are bidirectional, but some can only do 1 direction.
 */
#define SND_DESC_DAI_DIR_PLAYBACK	0
#define SND_DESC_DAI_DIR_CAPTURE	1
#define SND_DESC_DAI_DIR_BIDIRECTIONAL	2

/*
 * DAI Clock master with respect to the ???
 * Master with respect to host but can be on codec side also
 * both FRAME or BClK master
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
 * both polarity
 */
#define SND_DESC_DAI_POL_LOW	0
#define SND_DESC_DAI_POL_HIGH	1

/*
 * GPIO Functions.
 * Any functon that can be contolled or sensed by an audio GPIO.
 */
#define SND_DESC_GPIO_FUNC_HEADPHONES	0
#define SND_DESC_GPIO_FUNC_HEADSET	1
#define SND_DESC_GPIO_FUNC_SPEAKERS	2
#define SND_DESC_GPIO_FUNC_LINE_IN	3
#define SND_DESC_GPIO_FUNC_LINE_OUT	4
#define SND_DESC_GPIO_FUNC_EXT_AMP	5
#define SND_DESC_GPIO_FUNC_SWITCH	6

/*
 * Generic String Descriptor
 * This descriptor can be used to hold general purpose data that does not easily
 * fit into the other descriptors.
 * TODO: Will the text format be driver specific or will we have a standard syntax ??
 * TODO: What data will we store here ? Assuming, pins, jack connections ??
 */
struct snd_desc_str {
	u32 capabilities_size;
	u8 capabilities[0];
}__attribute__((packed, aligned(1)));


/*
 * PCM stream link parameters descriptor.
 * Stream SW params.
 */
struct snd_desc_pcm_params {
	u16 format_tag;		/* waveform-audio format */
	u16 channels;		/* number of channels */
	u32 sample_per_second;	/* sample rate */
	u32 byte_per_second;	/* Required since value may be different from bits_per_sample * sample_per_second / 8 */
	u16 block_allign;	/* Block allignment in bytes*/
	u16 frame_size;		/* bits per sample */
	u16 size;		/* Size in bytes of extra format information appended */
	u16 sample_size; 	/* Size in bits*/
	u32 channel_mask;	/* specify assignment of channels in the stream to speaker positions */
	u8 sub_format[16];	/* subformat of data may be PCM format or vendor specific */
}__attribute__((packed, aligned(1)));	

/*
 * PCM stream link configuration descriptor - Single stream descriptor.
 */
struct snd_desc_pcm_config {
	struct snd_desc_pcm_params params;
	struct snd_desc_str string;
}__attribute__((packed, aligned(1)));


/*
 * PCM stream links - Multiple stream descriptors.
 */
struct snd_desc_pcm_configs {
	u8 num_configs;		/* number of stream descriptor configs */
	u8 reserved[3];
	struct snd_desc_pcm_config config[0];
}__attribute__((packed, aligned(1)));


/*
 * NHLT Endpoint
 * NHLT Non HD-audio Link Table
 */
struct snd_desc_nhlt_endpoint {
	u32 length;		/* length of structure in bytes */
	u8 linktype;		/* SND_DESC_DAI_TYPE_ */
	u8 instanceid;          /* Unique identifier within NHLT table. Assigned in incremental order */
	u16 vendorid;           /* Virtual device vendor id */
	u16 deviceid;           /* Virtual device device id */
	u16 revisionid;         /* Virtual device revision id */
	u32 subsystemid;        /* Virtual device sbusstem id */
	u8 devicetype;          /* SND_DESC_DEVICE_TYPE */
	u8 direction; 		/* SND_DESC_DAI_DIR_ */
	u8 virtualbusid;
	struct snd_desc_pcm_configs configs;
	struct snd_desc_str string;
}__attribute__((packed, aligned(1)));


/*
 * HW DAI Link Config
 * Hardware DAI PHY configuration.
 */
struct snd_desc_dai_config {
	/* high level topology */
	u8 name[16];		/* name of DAI link */
	u8 linktype;		/* SND_DESC_DAI_TYPE_ */
	u8 virtual_bus_id;	/* virtual bus ID of DAI link */
	u8 reserved1[2];
	u8 codec_port[8];	/* ID of codec port */

	/* link hardware format and clocking */
	u8 host_bclk_master; 	/* SND_DESC_DAI_CLK_ wrt host */
	u8 host_frame_master; 	/* SND_DESC_DAI_CLK_ wrt host*/
	u8 protocol;            /* SND_DESC_DAI_PROT_ */
	u8 frame_polarity;	/* SND_DESC_DAI_POL_ */
	u8 bclk_polarity;       /* SND_DESC_DAI_POL_ */
	u8 data_polarity;	/* SND_DESC_DAI_POL_ */
	u8 start_delay;		/* start delay after FRAME */
	u8 reserved2[1];
	u32 frame_width;	/* frame width in bits */
	u32 frame_rate;		/* frames per second */

	/* Time division multiplexing */
	u8 tdm_slots;		/* number of TDM slots in use */
	u8 bit_per_slots;	/* width of TDM slot in bits */
	u8 reserved3[2];
	u32 active_tx_slots;	/* bitmap of active host Tx plots */
	u32 active_rx_slots;	/* bitmap of active host Rx plots */
}__attribute__((packed, aligned(1)));

/*
 * HW DAI Links config
 * DAI link table.
 */
struct snd_desc_dai_descriptor {
	u32 num_dai_link;
	struct	snd_desc_dai_config link[0];
}__attribute__((packed, aligned(1)));


/*
 * Platform GPIO
 */
struct snd_desc_gpio {
	u16 gpio_number;		/* GPIO host number */
	u8 gpio_function;		/* SND_DESC_GPIO_FUNC_ */
	u8 gpio_id;			/* GPIO ID number, i.e. hp0, hp1 */
} __attribute__((packed, aligned(1)));

/*
 * Array of Platform GPIOs
 */
struct snd_desc_platform_gpios {
	u32 num_gpios;
	struct snd_desc_gpio gpio[0];
} __attribute__((packed, aligned(1)));

/*
 * Platform Pin
 */
struct snd_desc_pin {
	u8 source[16];			/* Source pin name */
	u8 sink[16];			/* Sink pin name */
} __attribute__((packed, aligned(1)));

/*
 * Array of Platform Pins
 */
struct snd_desc_platform_pins {
	u32 num_pins;
	struct snd_desc_pin pin[0];
} __attribute__((packed, aligned(1)));



/* client component driver API - called by codec, platform drivers */

/* lets use the new component structure for handle, if it's not ready upstream 
 * we can help or use existing codec, platform varients */

/* we have snd_descriptor_add_() functions for each descriptor structure */

int snd_descriptor_new_dai(struct snd_component *c,
        struct snd_desc_dai_descriptor *dai_desc);

int snd_des_new_nhlt(struct snd_component *c,
        struct snd_desc_nhlt_endpoint *pcm_desc);

int snd_des_new_gpios(struct snd_component *c,
        struct snd_desc_platform_gpios *gpios);

int snd_des_new_pins(struct snd_component *c,
        struct snd_desc_platform_pins *pins);

/* should be called when driver module is removed */
void snd_descriptor_free_component(struct snd_component *c);


/* machine driver API - one call for each descriptor type */

int snd_descriptor_get_dai(struct snd_card *card, const char *component,
        const struct snd_desc_dai_descriptor **dai_desc);

int snd_descriptor_get_pcm(struct snd_card *card, const char *component,
        const struct snd_desc_nhlt_endpoint **pcm_desc);

int snd_descriptor_get_gpios(struct snd_card *card, const char *component,
        const struct snd_desc_platform_gpios **gpio_desc);

int snd_descriptor_get_pins(struct snd_card *card, const char *component,
        const struct snd_desc_plaform_pins **pin_desc);

/*..... more machine driver APIs here */


