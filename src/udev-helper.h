#ifndef __UDEV_HELPER_H__
#define __UDEV_HELPER_H__
typedef void (*UhCallback)(gint, gint, gpointer);


int uh_init();
int uh_destroy();
void uh_set_callback(UhCallback cb, gpointer data);
void uh_query_state(gint*, gint*);

/* TODO: Implement this */
char *uh_get_device_name();

enum {
    USB_MODE_UNKNOWN = 0,

    USB_MODE_B_IDLE = 1,
    USB_MODE_B_PERIPHERAL = 2,
    USB_MODE_B_HOST= 3,

    USB_MODE_A_IDLE = 4,
    USB_MODE_A_PERIPHERAL = 5,
    USB_MODE_A_HOST = 6,
};

enum {
    USB_SUPPLY_UNKNOWN = 0,
    USB_SUPPLY_NONE = 1,
    USB_SUPPLY_CDP = 2,
    USB_SUPPLY_DCP = 3,
};

#endif /* __UDEV_HELPER_H__ */
