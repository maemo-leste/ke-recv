sbin_PROGRAMS = ke-recv
bin_PROGRAMS  = ke-recv-test
libexec_PROGRAMS = mmc-format mmc-check
libexec_SCRIPTS  = mmc-mount mmc-rename

AM_CFLAGS = $(DEPS_CFLAGS)
LDADD     = $(DEPS_LIBS)

AM_CPPFLAGS = \
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\"

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
