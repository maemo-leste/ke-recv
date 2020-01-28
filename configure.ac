AC_INIT(src/ke-recv.c)
AM_INIT_AUTOMAKE(ke-recv, 2.18)
AM_CONFIG_HEADER(config.h)
AC_PROG_CC

GETTEXT_PACKAGE=ke-recv
AC_SUBST(GETTEXT_PACKAGE)
ALL_LINGUAS="de_DE en_GB en_US es_ES es_MX fi_FI fr_FR fr_CA it_IT pt_BR ru_RU"
AM_GLIB_GNU_GETTEXT

PKG_CHECK_MODULES([DEPS], [libosso glib-2.0 gconf-2.0 dbus-1 gtk+-2.0
                           hildon-1 libhildonmime gudev-1.0 libevdev
                           gnome-vfs-2.0 gio-2.0 dbus-glib-1])

AC_OUTPUT([Makefile src/Makefile mmc-utils/Makefile])
