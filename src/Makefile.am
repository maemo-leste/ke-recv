sbin_PROGRAMS = ke-recv mmc-format mmc-check
sbin_SCRIPTS = mmc-mount
bin_PROGRAMS = ke-recv-test

INCLUDES = \
	`pkg-config --cflags libosso` \
	`pkg-config --cflags glib-2.0` \
	`pkg-config --cflags gconf-2.0` \
	`pkg-config --cflags dbus-1` \
	`pkg-config --cflags gtk+-2.0` \
	`pkg-config --cflags hildon-1` \
	`pkg-config --cflags hal` \
	`pkg-config --cflags libhildonmime` \
	-DLOCALEDIR=\"`pkg-config --variable=localedir osso-af-settings`\"

ke_recv_LDFLAGS = \
	`pkg-config --libs libosso` \
	`pkg-config --libs hal` \
	`pkg-config --libs libhildonmime` \
	`pkg-config --libs gconf-2.0` \
	`pkg-config --libs libvolume_id`
mmc_format_LDFLAGS = \
	`pkg-config --libs hildon-1`

mmc_check_LDFLAGS = \
	`pkg-config --libs hildon-1`

ke_recv_test_LDFLAGS = \
	`pkg-config --libs libosso`

ke_recv_SOURCES = \
        ke-recv.h \
        exec-func.h \
        gui.h \
        events.h \
	fat-tools.h \
	swap_mgr.h \
	camera.h \
	ke-recv.c \
	exec-func.c \
	gui.c \
	events.c \
	swap_mgr.c \
	camera.c \
	fat-tools.c

ke_recv_test_SOURCES = \
        ke-recv.h \
	ke-recv-test.c

mmc_format_SOURCES = \
	ke-recv.h \
	mmc-format.h \
	exec-func.h \
	mmc-format.c \
	exec-func.c

mmc_check_SOURCES = \
	ke-recv.h \
	mmc-check.c

