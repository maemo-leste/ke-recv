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

/sbin/lsmod | grep -E 'g_file_storage|g_mass_storage' > /dev/null
if [ $? != 0 ]; then
  exit 0
fi

GADGETPATH='/sys/devices/platform/musb_hdrc/gadget'
LUN0='gadget-lun0'
LUN1='gadget-lun1'

if [ ! -e $GADGETPATH ]; then
  GADGETPATH='/sys/devices/platform/musb-omap2430/musb-hdrc.0.auto/gadget'
  LUN0='lun0'
  LUN1='lun1'
fi

for lun in $LUN0 $LUN1; do
  STR=`cat $GADGETPATH/$lun/file`
  if [ "x$STR" != "x" ]; then
    exit 1
  fi
done

exit 0
