#include "canlab.h"
#include "canlab_socketcan.h"
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
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

static const char *skip_space(const char *cursor) {
    while (isspace((unsigned char)*cursor)) { ++cursor; }
    return cursor;
}

static cb_result number(const char **cursor, int base, uint32_t maximum, uint32_t *value) {
    const char *start = skip_space(*cursor);
    char *end = NULL;
    unsigned long parsed;
    if (*start == '-' || *start == '+') { return CB_ARG; }
    errno = 0;
    parsed = strtoul(start, &end, base);
    if (end == start || (*end != '\0' && !isspace((unsigned char)*end))) { return CB_ARG; }
    if (errno == ERANGE || parsed > maximum) { return CB_RANGE; }
    *value = (uint32_t)parsed;
    *cursor = end;
    return CB_OK;
}

static uint8_t hex_digit(char character) {
    if (character >= '0' && character <= '9') { return (uint8_t)(character - '0'); }
    return (uint8_t)(tolower((unsigned char)character) - 'a' + 10);
}

static cb_result memory_command(const char *line, cb_memory_endpoint *client, cb_node *node, uint32_t *now_ms) {
    uint32_t id = 0U;
    uint32_t dlc = 0U;
    const char *cursor = line + 1;
    uint32_t next_time = 0U;
    if (line[0] == 'T' && isspace((unsigned char)line[1])) {
        cb_result result = number(&cursor, 10, UINT32_MAX, &next_time);
        if (result != CB_OK) { return result; }
        if (*skip_space(cursor) != '\0') { return CB_ARG; }
        result = cb_node_poll(node, next_time);
        if (result != CB_TIME) { *now_ms = next_time; }
        return result;
    }
    if (line[0] == 'F' && isspace((unsigned char)line[1])) {
        cb_frame frame = {0};
        const char *hex;
        size_t hex_length;
        cb_result parsed;
        unsigned i;
        parsed = number(&cursor, 16, 0x7FFU, &id);
        if (parsed != CB_OK) { return parsed; }
        parsed = number(&cursor, 10, UINT32_MAX, &dlc);
        if (parsed != CB_OK) { return parsed; }
        hex = skip_space(cursor);
        hex_length = strcspn(hex, " \t\r\n");
        if (dlc > 8U || hex_length != dlc * 2U) { return CB_LENGTH; }
        if (*skip_space(hex + hex_length) != '\0') { return CB_ARG; }
        frame.id = (uint16_t)id;
        frame.dlc = (uint8_t)dlc;
        for (i = 0U; i < dlc; ++i) {
            if (!isxdigit((unsigned char)hex[2U * i]) || !isxdigit((unsigned char)hex[2U * i + 1U])) { return CB_ARG; }
            frame.data[i] = (uint8_t)((hex_digit(hex[2U * i]) << 4U) | hex_digit(hex[2U * i + 1U]));
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
        (void)fputs("Usage: can_node --memory | --socketcan INTERFACE\n", stderr);
        return 2;
    }
    cb_memory_init(&memory);
    if (socket_mode) {
        if (cb_socketcan_open(&socket_bus, argv[2]) != CB_OK) {
            (void)fputs("Cannot open SocketCAN interface ", stderr);
            (void)fputs(argv[2], stderr);
            (void)fputc('\n', stderr);
            return 1;
        }
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
