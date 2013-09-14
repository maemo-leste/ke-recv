#!/bin/sh
# This file is part of ke-recv
#
# Copyright (C) 2008-2009 Nokia Corporation. All rights reserved.
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

/sbin/lsmod | grep g_file_storage > /dev/null
if [ $? = 0 ]; then
    logger "$0: removing g_file_storage"
    initctl emit G_FILE_STORAGE_REMOVE
    /sbin/rmmod g_file_storage
fi

/sbin/lsmod | grep g_mass_storage > /dev/null
if [ $? = 0 ]; then
    logger "$0: removing g_mass_storage"
    initctl emit G_FILE_STORAGE_REMOVE
    /sbin/rmmod g_mass_storage
fi

/sbin/lsmod | grep g_nokia > /dev/null
if [ $? != 0 ]; then
    /sbin/modprobe g_nokia
    if [ $? != 0 ]; then
        logger "$0: failed to install g_nokia"
        exit 1
    else
        sleep 2
    fi
fi

[ -f /etc/default/usbnetwork ] && . /etc/default/usbnetwork
if [ "$USBNETWORK_ENABLE" = "1" ]; then
    ifup usb0
    if [ $? != 0 ]; then
        logger "$0: ifup usb0 failed"
    else
        IP_ADDR="$(ifconfig usb0 | sed -n 's/.*inet addr:\([0-9\.]*\).*/\1/p')"
        if [ -z "$IP_ADDR" ]; then
            logger "$0: no ip address for usb0"
        else
            IP_GW="$(route -n | sed -n 's/^0\.0\.0\.0\s*\([0-9\.]*\).*usb0$/\1/p')"
            if [ -z "$IP_GW" ]; then IP_GW=${IP_ADDR%.*}.$((${IP_ADDR##*.}-1)); fi
            echo "nameserver $IP_GW" > /var/run/resolv.conf.usb0
            if ! grep -q "/var/run/resolv.conf.usb0" /etc/dnsmasq.conf; then
                logger "$0: restarting dnsmasq"
                echo "resolv-file=/var/run/resolv.conf.usb0" >> /etc/dnsmasq.conf
                stop -q dnsmasq
                start -q dnsmasq
            fi
            if [ "$USBNETWORK_NAT" = "1" ]; then
                if [ -f /proc/sys/net/ipv4/ip_forward -a -x /usr/sbin/iptables ]; then
                    echo 1 > /proc/sys/net/ipv4/ip_forward
                    /usr/sbin/iptables -t nat -A POSTROUTING ! -o lo -j MASQUERADE
                fi
            fi
            DNSMASQ_ARGS=""
            if [ "$USBNETWORK_DHCP" = "1" ]; then
                DNSMASQ_ARGS="$DNSMASQ_ARGS -F $IP_GW,$IP_GW -K -9 -l /dev/null"
                if [ "$USBNETWORK_DNS" != "1" ]; then
                    DNSMASQ_ARGS="$DNSMASQ_ARGS -p 0"
                fi
            fi
            if [ "$USBNETWORK_DHCP" = "1" -o "$USBNETWORK_DNS" = "1" ]; then
                /usr/sbin/dnsmasq -i usb0 -I lo -a $IP_ADDR -z -x /var/run/dnsmasq.pid.usb0 -C /dev/null $DNSMASQ_ARGS
            fi
            logger "$0: USB network enabled, local address: $IP_ADDR, remote address: $IP_GW"
        fi
    fi
fi

initctl emit --no-wait G_NOKIA_READY

# Wait until daemons are up
INC=1
while [ "x$(pidof pnatd)x" = "xx" -o "x$(pidof obexd)x" = "xx" \
       -o "x$(pidof syncd)x" = "xx" ]; do
    if [ $INC -gt 20 ]; then
      echo "$0: Error, daemons did not start"
      logger "$0: Error, daemons did not start"
      exit 1
    fi
    sleep 1
    INC=`expr $INC + 1`
done

OBEXD_PID=`pidof obexd`
if [ $? != 0 ]; then
    logger "$0: obexd is not running"
else
    kill -USR1 $OBEXD_PID
    logger "$0: sent SIGUSR1 to obexd"
fi

SYNCD_PID=`pidof syncd`
if [ $? != 0 ]; then
    logger "$0: failed to get syncd's PID"
    exit 1
fi

kill -USR1 $SYNCD_PID
logger "$0: sent SIGUSR1 to syncd"

exit 0
