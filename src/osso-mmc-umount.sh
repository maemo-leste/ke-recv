#!/bin/sh
# This file is part of ke-recv
#
# Copyright (C) 2005-2009 Nokia Corporation. All rights reserved.
#
# Author: Kimmo Hämäläinen <kimmo.hamalainen@nokia.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License 
# version 2 as published by the Free Software Foundation. 
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
# 02110-1301 USA

if [ $# -lt 1 ]; then
  echo "Usage: $0 <mount point | device> [\"lazy\"]"
  exit 1
fi

MP=$1

grep "$MP " /proc/mounts > /dev/null
if [ $? = 0 ]; then
  # first try in gvfs way
  mmc-unmount $MP 2> /dev/null
  RC=$?
  if [ $RC != 0 -a $# = 2 ]; then
    # lazy unmounting if mmc-unmount failed
    echo "$0: lazy umount for $MP"
    umount -l $MP 2> /dev/null
    RC=$?
    if [ $RC != 0 -a -x /usr/bin/lsof ]; then
      TMP=`/usr/bin/lsof $MP`
      echo $TMP
      if [ -x /usr/bin/logger ]; then
        /usr/bin/logger "x$TMP"
      fi
    fi
  elif [ $RC != 0 ]; then
    # old-fashioned unmounting if mmc-unmount failed
    umount $MP 2> /dev/null
    RC=$?
    if [ $RC != 0 -a -x /usr/bin/lsof ]; then
      TMP=`/usr/bin/lsof $MP`
      echo $TMP
      if [ -x /usr/bin/logger ]; then
        /usr/bin/logger "x$TMP"
      fi
    fi
  fi
else
  # it is not mounted
  RC=0
fi

exit $RC
