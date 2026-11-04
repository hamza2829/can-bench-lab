#include "canlab.h"
#include "canlab_socketcan.h"
#include <ctype.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint32_t clock_ms(void) {
    struct timespec time_value;
    if (clock_gettime(CLOCK_MONOTONIC, &time_value) != 0) { return 0U; }
    return (uint32_t)((uint64_t)time_value.tv_sec * 1000U + (uint64_t)time_value.tv_nsec / 1000000U);
}

static void drain(cb_memory_endpoint *endpoint) {
    cb_frame frame;
    while (cb_memory_receive(endpoint, &frame) == CB_OK) {
        uint8_t i;
        (void)printf("FRAME %03X %u ", (unsigned)frame.id, (unsigned)frame.dlc);
        for (i = 0U; i < frame.dlc; ++i) { (void)printf("%02X", (unsigned)frame.data[i]); }
        (void)putchar('\n');
    }
}

static cb_result memory_command(const char *line, cb_memory_endpoint *client, cb_node *node, uint32_t *now_ms) {
    unsigned id = 0U;
    unsigned dlc = 0U;
    char hex[18] = {0};
    uint32_t next_time = 0U;
    if (sscanf(line, "T %" SCNu32, &next_time) == 1) {
        cb_result result = cb_node_poll(node, next_time);
        if (result != CB_TIME) { *now_ms = next_time; }
        return result;
    }
    if (sscanf(line, "F %x %u %17s", &id, &dlc, hex) >= 2) {
        cb_frame frame = {0};
        unsigned i;
        if (id > 0x7FFU) { return CB_RANGE; }
        if (dlc > 8U || strlen(hex) != dlc * 2U) { return CB_LENGTH; }
        frame.id = (uint16_t)id;
        frame.dlc = (uint8_t)dlc;
        for (i = 0U; i < dlc; ++i) {
            unsigned value;
            if (!isxdigit((unsigned char)hex[2U * i]) || !isxdigit((unsigned char)hex[2U * i + 1U])) { return CB_ARG; }
            if (sscanf(hex + 2U * i, "%2x", &value) != 1) { return CB_ARG; }
            frame.data[i] = (uint8_t)value;
        }
        {
            cb_result result = cb_memory_send(client, &frame);
            return result == CB_OK ? cb_node_poll(node, *now_ms) : result;
        }
    }
    return CB_ARG;
}

int main(int argc, char **argv) {
    cb_memory_bus memory;
    cb_memory_endpoint node_endpoint = {&memory, 0U};
    cb_memory_endpoint client_endpoint = {&memory, 1U};
    cb_socketcan socket_bus = {-1};
    cb_node node;
    cb_hal hal;
    bool socket_mode = argc == 3 && strcmp(argv[1], "--socketcan") == 0;
    uint32_t now_ms = socket_mode ? clock_ms() : 0U;
    char line[128];
    if (!socket_mode && !(argc == 2 && strcmp(argv[1], "--memory") == 0)) {
        (void)fprintf(stderr, "Usage: can_node --memory | --socketcan INTERFACE\n");
        return 2;
    }
    cb_memory_init(&memory);
    if (socket_mode) {
        if (cb_socketcan_open(&socket_bus, argv[2]) != CB_OK) { (void)fprintf(stderr, "Cannot open SocketCAN interface %s\n", argv[2]); return 1; }
        hal = cb_socketcan_hal(&socket_bus);
    } else { hal = cb_memory_hal(&node_endpoint); }
    if (cb_node_init(&node, 1U, hal, now_ms) != CB_OK || cb_node_start(&node) != CB_OK) {
        cb_socketcan_close(&socket_bus);
        return 1;
    }
    (void)setvbuf(stdout, NULL, _IOLBF, 0U);
    if (!socket_mode) { drain(&client_endpoint); }
    (void)puts("READY");
    for (;;) {
        struct pollfd input = {STDIN_FILENO, POLLIN, 0};
        int ready = poll(&input, 1U, socket_mode ? 2 : -1);
        if (ready < 0) { cb_socketcan_close(&socket_bus); return 1; }
        if (ready > 0 && (input.revents & (POLLIN | POLLHUP)) != 0) {
            if (fgets(line, sizeof(line), stdin) == NULL || strncmp(line, "QUIT", 4U) == 0) { break; }
            if (!socket_mode) {
                cb_result result = memory_command(line, &client_endpoint, &node, &now_ms);
                drain(&client_endpoint);
                (void)printf("DONE %d\n", (int)result);
            } else { (void)puts("DONE -1"); }
        }
        if (socket_mode) { (void)cb_node_poll(&node, clock_ms()); }
    }
    cb_socketcan_close(&socket_bus);
    return 0;
}
