sbin_PROGRAMS = ke-recv mmc-format mmc-check
sbin_SCRIPTS  = mmc-mount
bin_PROGRAMS  = ke-recv-test

AM_CFLAGS = $(DEPS_CFLAGS)
LDADD     = $(DEPS_LIBS)

AM_CPPFLAGS = -DLOCALEDIR=\"`pkg-config --variable=localedir osso-af-settings`\"

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
	fat-tools.c \
	udev-helper.h \
	udev-helper.c \
	kbd-slide.c \
	kbd-slide.h

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
