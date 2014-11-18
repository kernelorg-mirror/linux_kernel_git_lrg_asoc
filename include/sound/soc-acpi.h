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
 * Generic String Descriptor
 * This descriptor can be used to hold general purpose data that does not easily
 * fit into the other descriptors.
 * TODO: Will the text format be driver specific or will we have a standard syntax ??
 * TODO: What data will we store here ? Assuming, pins, jack connections ??
 */
struct snd_desc_str {
	u32 capabilities_size;
	u8 capabilities[0];	// TODO: this should be padded to 32bit boundaries
}__attribute__((packed, aligned(1)));


/*
 * PCM stream link parameters descriptor.
 * Stream SW params.
 */
struct snd_desc_pcm_params {
	u16 format_tag;		// TODO: what are values ??
	u16 channels;		/* number of channels */
	u32 sample_per_second;	/* sample rate */
	u32 byte_per_second;	// TODO: is this required since we have bits per sample and srate ??
	u16 block_allign;	// TODO: ??
	u16 bits_per_sample;	/* bits per sample TODO: reanme to frame size ? */
	u16 size;		// TODO: what size ??
	u16 valid_bit_per_sample; // TODO: rename to sample size ??
	u32 channel_mask;	/* channel mask for TDM or for 5.1 mapping ?? - overlap with structure below*/
	u8 sub_format[16];	// TODO: where is this defined ??
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
	u8 reserved[3]; 	// TODO: added this to give alignment
	struct snd_desc_pcm_config config[0];
}__attribute__((packed, aligned(1)));


/*
 * NHLT Endpoint
 * NHLT Non HD-audio Link Table
 */
struct snd_desc_nhlt_endpoint {
	u32 length;		/* length of structure in bytes */
	u16 deviceId;
        u8 linktype;		/* SND_DESC_DAI_TYPE_ */
	u8 virtualbusid;
	u8 direction; 		/* SND_DESC_DAI_DIR_ */
	struct snd_desc_pcm_configs configs;
	struct snd_desc_str string;
}__attribute__((packed, aligned(1)));


/*
 * HW DAI Link Config
 * Hardware DAI PHY configuration.
 */
struct snd_desc_dai_config {
	u8 name[16];		/* name of DAI link */
	u8 codec_port[4];	/* ID of codec port */
	u8 host_bclk_master; 	/* SND_DESC_DAI_CLK_ wrt host */
	u8 host_frame_master; 	/* SND_DESC_DAI_CLK_ wrt host*/
	u8 protocol;   		/* SND_DESC_DAI_PROT_ */
	u8 frame_polarity;	/* SND_DESC_DAI_POL_ TODO: what about bclk pol ?*/
	u8 reserved[3]; 	// TODO: changed from 2 to 3 to align
	u32 frame_width;	/* frame width in bits */
	u32 frame_rate;		/* frames per second */
	u8 data_polarity;	/* SND_DESC_DAI_POL_ */
	u8 tdm_slots;		/* number of TDM slots in use */
	u8 bit_per_slots;	/* width of TDM slot in bits */
	u8 start_delay;		/* start delay after FRAME */
	u32 active_tx_slots;	/* bitmap of active host Tx plots */
	u32 active_rx_slots;	/* bitmap of active host Rx plots */
}__attribute__((packed, aligned(1)));

/*
 * Multiple HW DAI link configs.
 */
struct snd_desc_dai_configs {
	u32	num_configs;
	struct	snd_desc_dai_config config[0];
}__attribute__((packed, aligned(1)));


/*
 * HW DAI Links configs - TODO: can this be combined with struct snd_desc_dai_configs
 */
struct snd_desc_dai_descriptor {
	u32 length;		/* length in bytes */
	u8 linktype;		/* SND_DESC_DAI_TYPE_ */
	u8 virtual_bus_id;
	struct snd_desc_dai_configs config;
}__attribute__((packed, aligned(1)));


/*
 * Platform routing
 *
 */
struct snd_desc_platform_routing {
	u32 audio_routing_length; 		/* routing info size in bytes */
	u8 jack_gpio_supported;                 /* This field is required as 0 can be a valid GPIO pin no. supported 0 not supported 1*/
	u8 jack_gpio_number; 			/* GPIO pin no. assigned for jack detection TODO : is u8 sufficient to hold pin no.*/
	u8 on_board_speaker_gpio_supported;  	/* supported 0 not supported 1 */
	u8 on_board_speaker_gpio_number; 	/* GPIO pin no. assigned for onboard speaker TODO : is u8 sufficient to hold pin no.*/
	u8 routing_info[0];	/* TODO : how routing info will be stored and size unknown */
}__attribute__((packed, aligned(1)));


/* client component driver API - called by codec, platform drivers */

/* lets use the new component structure for handle, if it's not ready upstream 
 * we can help or use existing codec, platform varients */

/* we have snd_descriptor_add_() functions for each descriptor structure */

int snd_descriptor_new_dai(struct snd_component *c,
        struct snd_desc_dai_descriptor *dai_desc);

/* TODO: should we rename to snd_desc_new_nhlt ?? */
int snd_descriptor_new_pcm(struct snd_component *c,
        struct snd_desc_nhlt_endpoint *pcm_desc);

/* should be called when driver module is removed */
void snd_descriptor_free_component(struct snd_component *c);

#if 0
// TODO: this needs to be worked out for pins
/*.....more client APIs here */

int snd_descriptor_add_pin(struct snd_component *c,
        struct snd_descriptor_pin *pin);

#endif

/* machine driver API - one call for each descriptor type */

int snd_descriptor_get_dai(struct snd_card *card,
        const struct snd_desc_dai_descriptor **dai_desc);

int snd_descriptor_get_pcm(struct snd_card *card,
        const struct snd_desc_nhlt_endpoint **pcm_desc);

/*..... more machine driver APIs here */


