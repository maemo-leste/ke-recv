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
  echo "Usage: $0 <mount point>"
  exit 1
fi

MP=$1

grep "$MP " /proc/mounts > /dev/null
if [ $? = 0 ]; then
  # first try in gvfs way
  mmc-unmount $MP 2> /dev/null
  if [ $? != 0 ]; then
    # try if old-fashioned way works
    umount $MP 2> /dev/null
  fi
  RC=$?
else
  # it is not mounted
  RC=0
fi

exit $RC
