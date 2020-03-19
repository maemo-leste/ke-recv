libexec_PROGRAMS = mmc-pre-unmount mmc-unmount

AM_CFLAGS = $(DEPS_CFLAGS)
LDADD     = $(DEPS_LIBS)

AM_CPPFLAGS = -DUSE_EXT_DBUS_DAEMON=1

mmc_pre_unmount_SOURCES = \
	mmc-pre-unmount.c

mmc_unmount_SOURCES = \
	mmc-unmount.c
