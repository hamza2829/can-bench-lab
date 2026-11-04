#ifndef CANLAB_SOCKETCAN_H
#define CANLAB_SOCKETCAN_H
#include "canlab.h"
typedef struct { int fd; } cb_socketcan;
cb_result cb_socketcan_open(cb_socketcan *bus, const char *interface_name);
void cb_socketcan_close(cb_socketcan *bus);
cb_hal cb_socketcan_hal(cb_socketcan *bus);
#endif
