#include "canlab.h"

cb_object *cb_od_find(cb_node *node, uint16_t index) {
    size_t i;
    if (node == NULL) { return NULL; }
    for (i = 0U; i < CB_OD_COUNT; ++i) {
        if (node->objects[i].index == index) { return &node->objects[i]; }
    }
    return NULL;
}

uint32_t cb_od_read(cb_node *node, uint16_t index, uint8_t subindex, uint32_t *value, uint8_t *size) {
    cb_object *object = cb_od_find(node, index);
    if (value == NULL || size == NULL) { return CB_ABORT_COMMAND; }
    if (object == NULL) { return CB_ABORT_NO_OBJECT; }
    if (subindex != 0U) { return CB_ABORT_SUBINDEX; }
    *value = object->value;
    *size = object->size;
    return 0U;
}

uint32_t cb_od_check_write(cb_node *node, uint16_t index, uint8_t subindex, uint8_t size, uint32_t value) {
    cb_object *object = cb_od_find(node, index);
    if (object == NULL) { return CB_ABORT_NO_OBJECT; }
    if (subindex != 0U) { return CB_ABORT_SUBINDEX; }
    if ((object->access & CB_OD_WRITE) == 0U) { return CB_ABORT_READ_ONLY; }
    if (size != object->size) { return CB_ABORT_TYPE; }
    if (value < object->minimum || value > object->maximum) { return CB_ABORT_RANGE; }
    if (index == 0x2002U && value != 0U && (node->state != CB_OPERATIONAL || node->objects[1].value != 0U)) { return CB_ABORT_STATE; }
    if (index == 0x2004U && node->objects[5].value != 0U) { return CB_ABORT_STATE; }
    return 0U;
}

uint32_t cb_od_write(cb_node *node, uint16_t index, uint8_t subindex, uint8_t size, uint32_t value, uint32_t now_ms) {
    uint32_t abort_code = cb_od_check_write(node, index, subindex, size, value);
    cb_object *object;
    if (abort_code != 0U) { return abort_code; }
    object = cb_od_find(node, index);
    if (object == NULL) { return CB_ABORT_NO_OBJECT; }
    if (index == 0x2004U) {
        if (value == 1U) { node->objects[1].value = 0U; node->sequence_valid = false; }
    } else {
        if (index == 0x2002U && value == 1U && object->value == 0U) { node->last_motion = now_ms; }
        object->value = value;
    }
    if (index == 0x2001U || index == 0x2002U) { node->last_command = now_ms; }
    return 0U;
}

static cb_result validate_map(cb_node *node, const cb_pdo_map *map, uint8_t mode, uint8_t *length) {
    uint8_t i;
    unsigned bits = 0U;
    if (node == NULL || map == NULL || length == NULL) { return CB_ARG; }
    if (map->count == 0U || map->count > 4U || map->cob_id > 0x7FFU) { return CB_RANGE; }
    for (i = 0U; i < map->count; ++i) {
        cb_object *object = cb_od_find(node, map->entries[i].index);
        uint8_t j;
        if (object == NULL || map->entries[i].subindex != 0U) { return CB_NOT_FOUND; }
        if ((object->access & mode) == 0U) { return CB_ACCESS; }
        if (map->entries[i].bits != object->size * 8U) { return CB_LENGTH; }
        for (j = 0U; j < i; ++j) {
            if (map->entries[i].index == map->entries[j].index) { return CB_ARG; }
        }
        bits += map->entries[i].bits;
    }
    if (bits > 64U) { return CB_LENGTH; }
    *length = (uint8_t)(bits / 8U);
    return CB_OK;
}

cb_result cb_pdo_pack(cb_node *node, const cb_pdo_map *map, cb_frame *frame) {
    uint8_t length = 0U;
    uint8_t offset = 0U;
    uint8_t i;
    cb_result result = validate_map(node, map, CB_OD_TPDO, &length);
    if (result != CB_OK) { return result; }
    if (frame == NULL) { return CB_ARG; }
    if (node->state != CB_OPERATIONAL) { return CB_STATE; }
    *frame = (cb_frame){map->cob_id, length, {0}};
    for (i = 0U; i < map->count; ++i) {
        cb_object *object = cb_od_find(node, map->entries[i].index);
        if (object == NULL) { return CB_NOT_FOUND; }
        cb_put_le(frame->data + offset, object->value, object->size);
        offset = (uint8_t)(offset + object->size);
    }
    return CB_OK;
}

cb_result cb_pdo_apply(cb_node *node, const cb_pdo_map *map, const cb_frame *frame, uint32_t now_ms) {
    uint8_t length = 0U;
    uint8_t offset = 0U;
    uint8_t i;
    uint32_t values[4] = {0};
    cb_result result = validate_map(node, map, CB_OD_RPDO, &length);
    if (result != CB_OK) { return result; }
    if (frame == NULL) { return CB_ARG; }
    if (node->state != CB_OPERATIONAL) { return CB_STATE; }
    if (frame->id != map->cob_id) { return CB_IGNORED; }
    if (frame->dlc != length) { return CB_LENGTH; }
    for (i = 0U; i < map->count; ++i) {
        uint8_t size = (uint8_t)(map->entries[i].bits / 8U);
        values[i] = cb_get_le(frame->data + offset, size);
        if (cb_od_check_write(node, map->entries[i].index, 0U, size, values[i]) != 0U) { return CB_RANGE; }
        offset = (uint8_t)(offset + size);
    }
    for (i = 0U; i < map->count; ++i) {
        uint32_t abort_code = cb_od_write(node, map->entries[i].index, 0U, (uint8_t)(map->entries[i].bits / 8U), values[i], now_ms);
        if (abort_code != 0U) { return CB_STATE; }
    }
    return CB_OK;
}
