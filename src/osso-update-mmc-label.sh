#!/bin/sh
# This file is part of ke-recv
#
# Copyright (C) 2004-2007 Nokia Corporation. All rights reserved.
#
# Contact: Kimmo Hämäläinen <kimmo.hamalainen@nokia.com>
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

DEV=$1
LABELFILE=$2
if [ "x$LABELFILE" = "x" ]; then
  LABELFILE="/tmp/.mmc-volume-label"
fi

# convert device name to an MS-DOS-style drive letter
L=`eval grep '\"$DEV\"' /etc/mtools.conf | awk '{print $2}' | sed 's/://'`
if [ "x$L" = x ]; then
  echo "$0: could not determine drive letter"
  exit 1
fi

# read the MMC volume label
TMP=`/usr/bin/mlabel -s $L: | awk '{gsub(/ Volume label is /, ""); print}'`
if [ "x$TMP" != x ]; then
  if [ "${TMP}x" = " Volume has no labelx" ]; then
    exec echo -n "" > "$LABELFILE"
  else
    exec echo -n "$TMP" > "$LABELFILE"
  fi
else
  echo "$0: could not read volume label from drive $L"
  echo -n "" > "$LABELFILE"
  exit 1
fi
