libexec_SCRIPTS = \
	fremantle-format-internal-memory-card.sh \
	osso-mmc-mount.sh \
	osso-mmc-umount.sh \
	osso-prepare-partition.sh \
	osso-usb-mass-storage-disable.sh \
	osso-usb-mass-storage-enable.sh \
	osso-usb-mass-storage-is-used.sh \
	pcsuite-disable.sh \
	pcsuite-enable.sh

%.sh: %.sh.in $(top_srcdir)/config.status
	sed 's,@LIBEXECDIR@,$(libexecdir),g' $< > $@
