all: mmc-states.png usb-host-states.png

mmc-states.png: mmc-states.dot
	dot -Tpng -Gmclimit=10.0 -Gnslimit=10.0 -o mmc-states.png < mmc-states.dot

usb-host-states.png: usb-host-states.dot
	dot -Tpng -Gmclimit=10.0 -Gnslimit=10.0 -o usb-host-states.png < usb-host-states.dot
