#include "canlab.h"

static cb_result heartbeat(cb_node *node, uint8_t state) {
    cb_frame frame = {(uint16_t)(0x700U + node->node_id), 1U, {0}};
    frame.data[0] = state;
    return node->hal.send(node->hal.context, &frame);
}

cb_result cb_node_init(cb_node *node, uint8_t id, cb_hal hal, uint32_t now_ms) {
    size_t i;
    static const cb_object defaults[CB_OD_COUNT] = {
        {0x1000U, 4U, CB_OD_READ, 0U, 0U, UINT32_MAX},
        {0x1001U, 1U, CB_OD_READ | CB_OD_TPDO, 0U, 0U, 255U},
        {0x1017U, 2U, CB_OD_READ | CB_OD_WRITE, 100U, 10U, 5000U},
        {0x2000U, 2U, CB_OD_READ | CB_OD_TPDO, 0U, 0U, 1000U},
        {0x2001U, 2U, CB_OD_READ | CB_OD_WRITE | CB_OD_RPDO, 0U, 0U, 1000U},
        {0x2002U, 1U, CB_OD_READ | CB_OD_WRITE | CB_OD_TPDO | CB_OD_RPDO, 0U, 0U, 1U},
        {0x2003U, 2U, CB_OD_READ | CB_OD_WRITE, 250U, 50U, 5000U},
        {0x2004U, 1U, CB_OD_READ | CB_OD_WRITE, 0U, 0U, 1U}
    };
    if (node == NULL || id == 0U || id > 127U || hal.send == NULL || hal.receive == NULL) { return CB_ARG; }
    *node = (cb_node){0};
    node->node_id = id;
    node->hal = hal;
    node->last_tick = now_ms;
    node->last_motion = now_ms;
    node->last_command = now_ms;
    node->last_heartbeat = now_ms;
    for (i = 0U; i < CB_OD_COUNT; ++i) { node->objects[i] = defaults[i]; }
    node->rpdo = (cb_pdo_map){(uint16_t)(0x200U + id), 2U, {{0x2001U, 0U, 16U}, {0x2002U, 0U, 8U}, {0}, {0}}};
    node->tpdo = (cb_pdo_map){(uint16_t)(0x180U + id), 3U, {{0x2000U, 0U, 16U}, {0x2002U, 0U, 8U}, {0x1001U, 0U, 8U}, {0}}};
    return CB_OK;
}

cb_result cb_node_start(cb_node *node) {
    cb_result result;
    if (node == NULL) { return CB_ARG; }
    if (node->state != CB_INITIALISING) { return CB_STATE; }
    result = heartbeat(node, 0U);
    if (result == CB_OK) { node->state = CB_PRE_OPERATIONAL; }
    return result;
}

cb_result cb_node_tpdo(cb_node *node) {
    cb_frame frame;
    cb_result result;
    if (node == NULL) { return CB_ARG; }
    result = cb_pdo_pack(node, &node->tpdo, &frame);
    if (result != CB_OK) { return result; }
    return node->hal.send(node->hal.context, &frame);
}

static cb_result nmt(cb_node *node, const cb_frame *frame, uint32_t now_ms) {
    uint8_t command;
    if (frame->dlc != 2U) { return CB_LENGTH; }
    if (frame->data[1] != 0U && frame->data[1] != node->node_id) { return CB_IGNORED; }
    command = frame->data[0];
    if (command == 129U) {
        cb_result result = cb_node_init(node, node->node_id, node->hal, now_ms);
        if (result != CB_OK) { return result; }
        return cb_node_start(node);
    }
    if (command == 130U) {
        node->state = CB_INITIALISING;
        node->objects[5].value = 0U;
        node->objects[2].value = 100U;
        node->sequence_valid = false;
        node->last_heartbeat = now_ms;
        return cb_node_start(node);
    }
    if (command == 1U) { node->state = CB_OPERATIONAL; }
    else if (command == 2U) { node->state = CB_STOPPED; }
    else if (command == 128U) { node->state = CB_PRE_OPERATIONAL; }
    else { return CB_ARG; }
    node->objects[5].value = 0U;
    node->last_motion = now_ms;
    node->last_heartbeat = now_ms;
    return heartbeat(node, (uint8_t)node->state);
}

static cb_result sdo(cb_node *node, const cb_frame *request, uint32_t now_ms) {
    cb_frame response = {(uint16_t)(0x580U + node->node_id), 8U, {0}};
    uint16_t index = 0U;
    uint8_t subindex = 0U;
    uint32_t abort_code = 0U;
    uint32_t value = 0U;
    uint8_t size = 0U;
    if (request->dlc >= 4U) {
        index = (uint16_t)cb_get_le(request->data + 1, 2U);
        subindex = request->data[3];
    }
    cb_put_le(response.data + 1, index, 2U);
    response.data[3] = subindex;
    if (request->dlc != 8U) { abort_code = CB_ABORT_COMMAND; }
    else if (request->data[0] == 0x80U) { return CB_IGNORED; }
    else if (request->data[0] == 0x40U) {
        abort_code = cb_od_read(node, index, subindex, &value, &size);
        if (abort_code == 0U) {
            response.data[0] = (uint8_t)(0x43U | ((4U - size) << 2U));
            cb_put_le(response.data + 4, value, size);
        }
    } else if (request->data[0] == 0x2FU || request->data[0] == 0x2BU || request->data[0] == 0x27U || request->data[0] == 0x23U) {
        size = (uint8_t)(4U - ((request->data[0] >> 2U) & 3U));
        value = cb_get_le(request->data + 4, size);
        abort_code = cb_od_write(node, index, subindex, size, value, now_ms);
        response.data[0] = 0x60U;
    } else { abort_code = CB_ABORT_COMMAND; }
    if (abort_code != 0U) {
        response.data[0] = 0x80U;
        cb_put_le(response.data + 4, abort_code, 4U);
    }
    return node->hal.send(node->hal.context, &response);
}

static cb_result fail_command(cb_node *node, uint8_t error, cb_result reason) {
    node->objects[1].value |= error;
    node->objects[5].value = 0U;
    (void)cb_node_tpdo(node);
    return reason;
}

cb_result cb_node_handle(cb_node *node, const cb_frame *frame, uint32_t now_ms) {
    cb_result result = cb_frame_valid(frame);
    if (node == NULL) { return CB_ARG; }
    if (result != CB_OK) { return result; }
    if (frame->id == 0U) { return nmt(node, frame, now_ms); }
    if (node->state != CB_PRE_OPERATIONAL && node->state != CB_OPERATIONAL) { return CB_IGNORED; }
    if (frame->id == 0x600U + node->node_id) { return sdo(node, frame, now_ms); }
    if (frame->id == node->rpdo.cob_id && node->state == CB_OPERATIONAL) {
        cb_frame payload = *frame;
        uint8_t checksum;
        if (frame->dlc != 5U) { return fail_command(node, 1U, CB_LENGTH); }
        checksum = (uint8_t)(0xA5U ^ frame->data[0] ^ frame->data[1] ^ frame->data[2] ^ frame->data[3]);
        if (checksum != frame->data[4]) { return fail_command(node, 1U, CB_RANGE); }
        if (node->sequence_valid && frame->data[3] != (uint8_t)(node->last_sequence + 1U)) { return fail_command(node, 0x10U, CB_RANGE); }
        payload.dlc = 3U;
        result = cb_pdo_apply(node, &node->rpdo, &payload, now_ms);
        if (result != CB_OK) { return fail_command(node, 1U, result); }
        node->sequence_valid = true;
        node->last_sequence = frame->data[3];
        node->last_command = now_ms;
        return cb_node_tpdo(node);
    }
    return CB_IGNORED;
}

cb_result cb_node_tick(cb_node *node, uint32_t now_ms) {
    cb_result result = CB_OK;
    uint32_t elapsed;
    if (node == NULL) { return CB_ARG; }
    elapsed = now_ms - node->last_tick;
    if (elapsed > UINT32_MAX / 2U) { return CB_TIME; }
    node->last_tick = now_ms;
    if (node->state == CB_INITIALISING) { return CB_OK; }
    if (node->state == CB_OPERATIONAL && node->objects[5].value != 0U) {
        if (now_ms - node->last_command >= node->objects[6].value) {
            node->objects[5].value = 0U;
            node->objects[1].value |= 0x10U;
            result = cb_node_tpdo(node);
        } else {
            uint32_t steps = (now_ms - node->last_motion) / 10U;
            uint32_t position = node->objects[3].value;
            uint32_t target = node->objects[4].value;
            if (steps > 0U) {
                uint32_t distance = position < target ? target - position : position - target;
                uint32_t change = steps < distance ? steps : distance;
                node->objects[3].value = position < target ? position + change : position - change;
                node->last_motion += steps * 10U;
                if (node->objects[3].value == target) { node->objects[5].value = 0U; }
                result = cb_node_tpdo(node);
            }
        }
    } else { node->last_motion = now_ms; }
    if (now_ms - node->last_heartbeat >= node->objects[2].value) {
        cb_result sent = heartbeat(node, (uint8_t)node->state);
        if (sent != CB_OK) { return sent; }
        node->last_heartbeat = now_ms;
    }
    return result;
}

cb_result cb_node_poll(cb_node *node, uint32_t now_ms) {
    unsigned i;
    cb_result first_error = CB_OK;
    if (node == NULL) { return CB_ARG; }
    if (now_ms - node->last_tick > UINT32_MAX / 2U) { return CB_TIME; }
    for (i = 0U; i < CB_QUEUE_CAPACITY; ++i) {
        cb_frame frame;
        cb_result result = node->hal.receive(node->hal.context, &frame);
        if (result == CB_EMPTY) { break; }
        if (result == CB_IGNORED) { continue; }
        if (result != CB_OK) { return result; }
        result = cb_node_handle(node, &frame, now_ms);
        if (result < 0 && first_error == CB_OK) { first_error = result; }
    }
    {
        cb_result tick = cb_node_tick(node, now_ms);
        return first_error != CB_OK ? first_error : tick;
    }
}
