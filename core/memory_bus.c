#include "canlab.h"

void cb_memory_init(cb_memory_bus *bus) {
    if (bus != NULL) { *bus = (cb_memory_bus){0}; }
}

cb_hal cb_memory_hal(cb_memory_endpoint *endpoint) {
    cb_hal hal = {endpoint, cb_memory_send, cb_memory_receive};
    return hal;
}

cb_result cb_memory_send(void *context, const cb_frame *frame) {
    cb_memory_endpoint *endpoint = context;
    unsigned destination;
    size_t tail;
    cb_result result = cb_frame_valid(frame);
    if (endpoint == NULL || endpoint->bus == NULL || endpoint->side > 1U) { return CB_ARG; }
    if (result != CB_OK) { return result; }
    destination = 1U - endpoint->side;
    if (endpoint->bus->count[destination] == CB_QUEUE_CAPACITY) { return CB_FULL; }
    tail = (endpoint->bus->head[destination] + endpoint->bus->count[destination]) % CB_QUEUE_CAPACITY;
    endpoint->bus->frames[destination][tail] = *frame;
    ++endpoint->bus->count[destination];
    return CB_OK;
}

cb_result cb_memory_receive(void *context, cb_frame *frame) {
    cb_memory_endpoint *endpoint = context;
    unsigned side;
    if (endpoint == NULL || endpoint->bus == NULL || endpoint->side > 1U || frame == NULL) { return CB_ARG; }
    side = endpoint->side;
    if (endpoint->bus->count[side] == 0U) { return CB_EMPTY; }
    *frame = endpoint->bus->frames[side][endpoint->bus->head[side]];
    endpoint->bus->head[side] = (endpoint->bus->head[side] + 1U) % CB_QUEUE_CAPACITY;
    --endpoint->bus->count[side];
    return CB_OK;
}
