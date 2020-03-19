#!/bin/sh
# This file is part of ke-recv
#
# Copyright (C) 2006 Nokia Corporation. All rights reserved.
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

MUSB="/sys/devices/platform/musb_hdrc"
MODE="$MUSB/mode"
LUN0="$MUSB/gadget/gadget-lun0/file"
LUN1="$MUSB/gadget/gadget-lun1/file"

if [ ! -e $MUSB ]; then
  MUSB="/sys/bus/platform/devices/musb-hdrc.0.auto"
  MODE="$MUSB/mode"
  LUN0="$MUSB/gadget/lun0/file"
  LUN1="$MUSB/gadget/lun1/file"
fi

for lun in $LUN0 $LUN1; do
  STR=`cat $lun`
  if [ "x$STR" != "x" ]; then
    exit 1
  fi
done

exit 0
