#!/bin/sh
# This file is part of ke-recv
#
# Copyright (C) 2005-2007 Nokia Corporation. All rights reserved.
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

PDEV=$1  ;# preferred device (partition)
MP=$2    ;# mount point

install_module() # $1 = kernel version $2 = modulename (no .ko)
{
  if lsmod | grep -qi $2; then return 0 ; fi

  if [ -f /mnt/initfs/lib/modules/$1/$2.ko ]; then
    if insmod /mnt/initfs/lib/modules/$1/$2.ko; then
      return 0
    fi
  fi

  return 1
}

grep "$PDEV " /proc/mounts > /dev/null
if [ $? = 0 ]; then
  echo "$0: $PDEV is already mounted"
  exit 0
fi

if [ ! -d $MP ]; then
  mkdir -p $MP
fi

mmc-mount $PDEV $MP
RC=$?
if [ $RC != 0 ]; then

  # Let's try ext3 - need to load modules
  KERNEL_VERSION=`uname -r`
  if install_module $KERNEL_VERSION mbcache; then
    if install_module $KERNEL_VERSION jbd; then
      if install_module $KERNEL_VERSION ext3; then
        mount -t ext3 $PDEV $MP > /dev/null
        RC=$?
      fi
    fi
  fi

  if [ $RC != 0 ]; then
    if install_module $KERNEL_VERSION ext2; then
      mount -t ext2 $PDEV $MP > /dev/null
      RC=$?
    fi
  fi

  if [ $RC = 0 ]; then
    chown user:users $MP
  fi
fi
exit $RC
