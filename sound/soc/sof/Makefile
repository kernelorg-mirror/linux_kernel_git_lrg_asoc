snd-sof-objs := core.o ops.o loader.o ipc.o pcm.o pm.o debug.o topology.o\
		control.o stream.o trace.o
snd-sof-intel-byt-objs := hw-byt.o
snd-sof-intel-hsw-objs := hw-hsw.o
snd-sof-intel-apl-objs := hw-apl.o

snd-sof-pci-objs := sof-pci-dev.o
snd-sof-acpi-objs := sof-acpi-dev.o
snd-sof-platform-objs := sof-platform-dev.o

obj-$(CONFIG_SND_SOC_SOF) += snd-sof.o
obj-$(CONFIG_SND_SOC_SOF_BAYTRAIL) += snd-sof-intel-byt.o
obj-$(CONFIG_SND_SOC_SOF_HASWELL) += snd-sof-intel-hsw.o
obj-$(CONFIG_SND_SOC_SOF_APOLLOLAKE) += snd-sof-intel-apl.o
