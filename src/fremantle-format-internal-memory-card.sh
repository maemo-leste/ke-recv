#!/bin/sh
# This file is part of ke-recv
#
# Copyright (C) 2008 Nokia Corporation. All rights reserved.
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

if [ "x$1" != "x/dev/mmcblk1" -a "x$1" != "x/dev/mmcblk0" ]; then
  echo "Usage: $0 <device name of internal memory card>"
  exit 1
fi

/etc/init.d/ke-recv stop
umount $1
umount ${1}p1
umount ${1}p2
umount ${1}p3
umount ${1}p4

sfdisk -D -uM $1 << EOF
,768,S
,4,L
,2048,L
,,b
EOF

mkdosfs -F 32 -R 38 ${1}p4

sync

echo "$0: done."
echo "$0: please reboot now."
