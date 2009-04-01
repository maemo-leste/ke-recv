bin_PROGRAMS = \
	mmc-pre-unmount \
	mmc-unmount

INCLUDES = \
	`pkg-config --cflags gnome-vfs-2.0 gio-2.0` \
	-I . \
	-DUSE_EXT_DBUS_DAEMON=1

mmc_pre_unmount_INCLUDES = \
	`pkg-config --cflags dbus-1 dbus-glib-1 gnome-vfs-2.0`

mmc_pre_unmount_SOURCES = \
	mmc-pre-unmount.c

mmc_pre_unmount_LDFLAGS = \
	-lgnomevfs-2

mmc_unmount_INCLUDES = \
	`pkg-config --cflags gio-2.0`

mmc_unmount_SOURCES = \
	mmc-unmount.c

mmc_unmount_LDFLAGS = \
	`pkg-config --libs gio-2.0`
