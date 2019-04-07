#!/bin/sh
# This file is part of ke-recv
#
# Copyright (C) 2008 Nokia Corporation. All rights reserved.
#
# Contact: Kimmo H�m�l�inen <kimmo.hamalainen@nokia.com>
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

logger "$0: removing pcsuite"

ifconfig usb0 down
/usr/sbin/hildon-usb-gadget-clear

#if [ -e /sys/module/g_nokia ]; then
#    [ -f /etc/default/usbnetwork ] && . /etc/default/usbnetwork
#    if [ "$USBNETWORK_ENABLE" = "1" ]; then
#        logger "$0: disable USB network"
#        if [ "$USBNETWORK_DHCP" = "1" ]; then
#            if [ -f /var/run/dnsmasq.pid.usb0 ]; then
#                DNSMASQ_PID=`cat /var/run/dnsmasq.pid.usb0`
#                rm -f /var/run/dnsmasq.pid.usb0
#                kill $DNSMASQ_PID
#            else
#                logger "$0: dnsmasq for USB network is not running"
#            fi
#        fi
#        if [ "$USBNETWORK_NAT" = "1" ]; then
#            if [ -f /proc/sys/net/ipv4/ip_forward -a -x /usr/sbin/iptables ]; then
#                echo 0 > /proc/sys/net/ipv4/ip_forward
#                /usr/sbin/iptables -t nat -D POSTROUTING ! -o lo -j MASQUERADE
#            fi
#        fi
#        rm -f /var/run/resolv.conf.usb0
#        ifdown usb0
#        ifconfig usb0 down 0.0.0.0
#    fi
#fi

exit 0
