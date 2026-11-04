#include "canlab_socketcan.h"
#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static cb_result send_frame(void *context, const cb_frame *frame) {
    cb_socketcan *bus = context;
    struct can_frame native = {0};
    uint8_t i;
    cb_result result = cb_frame_valid(frame);
    if (bus == NULL || bus->fd < 0) { return CB_ARG; }
    if (result != CB_OK) { return result; }
    native.can_id = frame->id;
    native.can_dlc = frame->dlc;
    for (i = 0U; i < frame->dlc; ++i) { native.data[i] = frame->data[i]; }
    return write(bus->fd, &native, sizeof(native)) == (ssize_t)sizeof(native) ? CB_OK : CB_IO;
}

static cb_result receive_frame(void *context, cb_frame *frame) {
    cb_socketcan *bus = context;
    struct can_frame native = {0};
    ssize_t length;
    uint8_t i;
    if (bus == NULL || bus->fd < 0 || frame == NULL) { return CB_ARG; }
    length = read(bus->fd, &native, sizeof(native));
    if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { return CB_EMPTY; }
    if (length != (ssize_t)sizeof(native)) { return CB_IO; }
    if ((native.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) != 0U) { return CB_IGNORED; }
    *frame = (cb_frame){(uint16_t)(native.can_id & CAN_SFF_MASK), native.can_dlc, {0}};
    if (frame->dlc > 8U) { return CB_LENGTH; }
    for (i = 0U; i < frame->dlc; ++i) { frame->data[i] = native.data[i]; }
    return CB_OK;
}

cb_result cb_socketcan_open(cb_socketcan *bus, const char *interface_name) {
    struct ifreq request = {0};
    struct sockaddr_can address = {0};
    size_t i;
    if (bus == NULL || interface_name == NULL || strlen(interface_name) >= IFNAMSIZ) { return CB_ARG; }
    bus->fd = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
    if (bus->fd < 0) { return CB_IO; }
    for (i = 0U; i < strlen(interface_name); ++i) { request.ifr_name[i] = interface_name[i]; }
    if (ioctl(bus->fd, SIOCGIFINDEX, &request) < 0) { cb_socketcan_close(bus); return CB_IO; }
    address.can_family = AF_CAN;
    address.can_ifindex = request.ifr_ifindex;
    if (bind(bus->fd, (struct sockaddr *)&address, sizeof(address)) < 0) { cb_socketcan_close(bus); return CB_IO; }
    return CB_OK;
}

void cb_socketcan_close(cb_socketcan *bus) {
    if (bus != NULL && bus->fd >= 0) { (void)close(bus->fd); bus->fd = -1; }
}

cb_hal cb_socketcan_hal(cb_socketcan *bus) {
    cb_hal hal = {bus, send_frame, receive_frame};
    return hal;
}
