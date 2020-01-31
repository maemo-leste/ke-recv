#!/bin/sh

set -x
glib-gettextize --copy --force
libtoolize --automake
intltoolize --copy --force --automake
aclocal
autoconf
autoheader
automake --add-missing --foreign
