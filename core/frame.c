#include "canlab.h"

cb_result cb_frame_valid(const cb_frame *frame) {
    if (frame == NULL) { return CB_ARG; }
    if (frame->id > 0x7FFU) { return CB_RANGE; }
    if (frame->dlc > 8U) { return CB_LENGTH; }
    return CB_OK;
}

cb_result cb_frame_encode(const cb_frame *frame, uint8_t *buffer, size_t length) {
    size_t i;
    cb_result result = cb_frame_valid(frame);
    if (result != CB_OK) { return result; }
    if (buffer == NULL) { return CB_ARG; }
    if (length < CB_WIRE_SIZE) { return CB_LENGTH; }
    for (i = 0U; i < CB_WIRE_SIZE; ++i) { buffer[i] = 0U; }
    buffer[0] = (uint8_t)frame->id;
    buffer[1] = (uint8_t)(frame->id >> 8U);
    buffer[2] = frame->dlc;
    for (i = 0U; i < frame->dlc; ++i) { buffer[3U + i] = frame->data[i]; }
    return CB_OK;
}

cb_result cb_frame_decode(cb_frame *frame, const uint8_t *buffer, size_t length) {
    cb_frame decoded = {0};
    cb_result result;
    size_t i;
    if (frame == NULL || buffer == NULL) { return CB_ARG; }
    if (length != CB_WIRE_SIZE) { return CB_LENGTH; }
    decoded.id = (uint16_t)((uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8U));
    decoded.dlc = buffer[2];
    result = cb_frame_valid(&decoded);
    if (result != CB_OK) { return result; }
    for (i = 0U; i < decoded.dlc; ++i) { decoded.data[i] = buffer[3U + i]; }
    *frame = decoded;
    return CB_OK;
}

uint32_t cb_get_le(const uint8_t *bytes, uint8_t size) {
    uint32_t result = 0U;
    uint8_t i;
    for (i = 0U; i < size && i < 4U; ++i) { result |= (uint32_t)bytes[i] << (8U * i); }
    return result;
}

void cb_put_le(uint8_t *bytes, uint32_t value, uint8_t size) {
    uint8_t i;
    for (i = 0U; i < size && i < 4U; ++i) { bytes[i] = (uint8_t)(value >> (8U * i)); }
}
