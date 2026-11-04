#include "canlab.h"
#include "unity.h"
#include <string.h>

static cb_memory_bus bus;
static cb_memory_endpoint endpoint = {&bus, 0U};
static cb_memory_endpoint client = {&bus, 1U};
static cb_node node;

static void clear_output(void) {
    cb_frame frame;
    while (cb_memory_receive(&client, &frame) == CB_OK) { }
}

void setUp(void) {
    cb_memory_init(&bus);
    TEST_ASSERT_EQUAL(CB_OK, cb_node_init(&node, 1U, cb_memory_hal(&endpoint), 0U));
    TEST_ASSERT_EQUAL(CB_OK, cb_node_start(&node));
    clear_output();
}
void tearDown(void) { }

static cb_frame sdo_request(uint8_t command, uint16_t index, uint32_t value) {
    cb_frame frame = {0x601U, 8U, {0}};
    frame.data[0] = command;
    cb_put_le(frame.data + 1, index, 2U);
    cb_put_le(frame.data + 4, value, 4U);
    return frame;
}

static cb_frame response(cb_frame request) {
    cb_frame result;
    TEST_ASSERT_EQUAL(CB_OK, cb_node_handle(&node, &request, 0U));
    TEST_ASSERT_EQUAL(CB_OK, cb_memory_receive(&client, &result));
    TEST_ASSERT_EQUAL_HEX16(0x581U, result.id);
    return result;
}

static void operational(void) {
    cb_frame frame = {0U, 2U, {1U, 1U}};
    TEST_ASSERT_EQUAL(CB_OK, cb_node_handle(&node, &frame, 0U));
    clear_output();
}

static cb_frame command(uint16_t target, uint8_t motor, uint8_t sequence) {
    cb_frame frame = {0x201U, 5U, {0}};
    cb_put_le(frame.data, target, 2U);
    frame.data[2] = motor;
    frame.data[3] = sequence;
    frame.data[4] = (uint8_t)(0xA5U ^ frame.data[0] ^ frame.data[1] ^ motor ^ sequence);
    return frame;
}

static void test_frame_roundtrip_and_padding(void) {
    cb_frame input = {0x7FFU, 3U, {0xABU, 0U, 0xFFU, 99U}};
    cb_frame output = {0};
    uint8_t wire[CB_WIRE_SIZE];
    TEST_ASSERT_EQUAL(CB_OK, cb_frame_encode(&input, wire, sizeof(wire)));
    TEST_ASSERT_EQUAL_HEX8(0xFFU, wire[0]);
    TEST_ASSERT_EQUAL_HEX8(7U, wire[1]);
    TEST_ASSERT_EQUAL(0U, wire[6]);
    TEST_ASSERT_EQUAL(CB_OK, cb_frame_decode(&output, wire, sizeof(wire)));
    TEST_ASSERT_EQUAL(input.id, output.id);
    TEST_ASSERT_EQUAL(input.dlc, output.dlc);
    TEST_ASSERT_EQUAL_MEMORY(input.data, output.data, 3U);
    TEST_ASSERT_EQUAL(0U, output.data[3]);
}

static void test_frame_boundaries(void) {
    cb_frame frame = {0U, 0U, {0}};
    uint8_t wire[CB_WIRE_SIZE] = {0};
    unsigned dlc;
    for (dlc = 0U; dlc <= 8U; ++dlc) {
        frame.dlc = (uint8_t)dlc;
        TEST_ASSERT_EQUAL(CB_OK, cb_frame_encode(&frame, wire, sizeof(wire)));
        TEST_ASSERT_EQUAL(CB_OK, cb_frame_decode(&frame, wire, sizeof(wire)));
    }
    frame.id = 0x800U;
    TEST_ASSERT_EQUAL(CB_RANGE, cb_frame_valid(&frame));
    TEST_ASSERT_EQUAL(CB_RANGE, cb_frame_encode(&frame, wire, sizeof(wire)));
    frame.id = 1U; frame.dlc = 9U;
    TEST_ASSERT_EQUAL(CB_LENGTH, cb_frame_valid(&frame));
    wire[1] = 0x80U;
    TEST_ASSERT_EQUAL(CB_RANGE, cb_frame_decode(&frame, wire, sizeof(wire)));
    wire[1] = 0U; wire[2] = 9U;
    TEST_ASSERT_EQUAL(CB_LENGTH, cb_frame_decode(&frame, wire, sizeof(wire)));
}

static void test_frame_invalid_arguments(void) {
    cb_frame frame = {1U, 1U, {0}};
    uint8_t wire[CB_WIRE_SIZE] = {0};
    TEST_ASSERT_EQUAL(CB_ARG, cb_frame_valid(NULL));
    TEST_ASSERT_EQUAL(CB_ARG, cb_frame_encode(NULL, wire, sizeof(wire)));
    TEST_ASSERT_EQUAL(CB_ARG, cb_frame_encode(&frame, NULL, sizeof(wire)));
    TEST_ASSERT_EQUAL(CB_LENGTH, cb_frame_encode(&frame, wire, 1U));
    TEST_ASSERT_EQUAL(CB_ARG, cb_frame_decode(NULL, wire, sizeof(wire)));
    TEST_ASSERT_EQUAL(CB_ARG, cb_frame_decode(&frame, NULL, sizeof(wire)));
    TEST_ASSERT_EQUAL(CB_LENGTH, cb_frame_decode(&frame, wire, 0U));
}

static void test_memory_fifo_wrap_and_full(void) {
    cb_frame frame = {1U, 1U, {0}};
    cb_frame out;
    unsigned round;
    for (round = 0U; round < 2U; ++round) {
        unsigned i;
        for (i = 0U; i < CB_QUEUE_CAPACITY; ++i) {
            frame.data[0] = (uint8_t)i;
            TEST_ASSERT_EQUAL(CB_OK, cb_memory_send(&client, &frame));
        }
        TEST_ASSERT_EQUAL(CB_FULL, cb_memory_send(&client, &frame));
        for (i = 0U; i < CB_QUEUE_CAPACITY; ++i) {
            TEST_ASSERT_EQUAL(CB_OK, cb_memory_receive(&endpoint, &out));
            TEST_ASSERT_EQUAL(i, out.data[0]);
        }
        TEST_ASSERT_EQUAL(CB_EMPTY, cb_memory_receive(&endpoint, &out));
    }
}

static void test_memory_invalid_arguments(void) {
    cb_frame frame = {0U, 9U, {0}};
    cb_memory_endpoint bad = {NULL, 0U};
    cb_memory_init(NULL);
    TEST_ASSERT_EQUAL(CB_ARG, cb_memory_send(NULL, &frame));
    TEST_ASSERT_EQUAL(CB_ARG, cb_memory_send(&bad, &frame));
    bad.bus = &bus; bad.side = 2U;
    TEST_ASSERT_EQUAL(CB_ARG, cb_memory_send(&bad, &frame));
    TEST_ASSERT_EQUAL(CB_LENGTH, cb_memory_send(&client, &frame));
    TEST_ASSERT_EQUAL(CB_ARG, cb_memory_receive(NULL, &frame));
    TEST_ASSERT_EQUAL(CB_ARG, cb_memory_receive(&bad, &frame));
    TEST_ASSERT_EQUAL(CB_ARG, cb_memory_receive(&client, NULL));
}

static void test_initial_state_and_bootup(void) {
    cb_frame frame;
    TEST_ASSERT_EQUAL(CB_OK, cb_node_init(&node, 42U, cb_memory_hal(&endpoint), 0U));
    TEST_ASSERT_EQUAL(CB_INITIALISING, node.state);
    TEST_ASSERT_EQUAL(CB_OK, cb_node_tick(&node, 1U));
    TEST_ASSERT_EQUAL(CB_OK, cb_node_start(&node));
    TEST_ASSERT_EQUAL(CB_PRE_OPERATIONAL, node.state);
    TEST_ASSERT_EQUAL(CB_OK, cb_memory_receive(&client, &frame));
    TEST_ASSERT_EQUAL_HEX16(0x72AU, frame.id);
    TEST_ASSERT_EQUAL(0U, frame.data[0]);
    TEST_ASSERT_EQUAL(CB_STATE, cb_node_start(&node));
}

static void test_node_invalid_arguments(void) {
    cb_hal hal = cb_memory_hal(&endpoint);
    cb_frame frame = {0U, 0U, {0}};
    TEST_ASSERT_EQUAL(CB_ARG, cb_node_init(NULL, 1U, hal, 0U));
    TEST_ASSERT_EQUAL(CB_ARG, cb_node_init(&node, 0U, hal, 0U));
    TEST_ASSERT_EQUAL(CB_ARG, cb_node_init(&node, 128U, hal, 0U));
    hal.send = NULL;
    TEST_ASSERT_EQUAL(CB_ARG, cb_node_init(&node, 1U, hal, 0U));
    TEST_ASSERT_EQUAL(CB_ARG, cb_node_start(NULL));
    TEST_ASSERT_EQUAL(CB_ARG, cb_node_tpdo(NULL));
    TEST_ASSERT_EQUAL(CB_ARG, cb_node_handle(NULL, &frame, 0U));
    TEST_ASSERT_EQUAL(CB_ARG, cb_node_handle(&node, NULL, 0U));
    TEST_ASSERT_EQUAL(CB_ARG, cb_node_tick(NULL, 0U));
    TEST_ASSERT_EQUAL(CB_ARG, cb_node_poll(NULL, 0U));
}

static void test_nmt_target_broadcast_and_states(void) {
    cb_frame nmt = {0U, 2U, {1U, 2U}};
    TEST_ASSERT_EQUAL(CB_IGNORED, cb_node_handle(&node, &nmt, 0U));
    nmt.data[1] = 0U;
    TEST_ASSERT_EQUAL(CB_OK, cb_node_handle(&node, &nmt, 0U));
    TEST_ASSERT_EQUAL(CB_OPERATIONAL, node.state);
    nmt.data[0] = 2U;
    TEST_ASSERT_EQUAL(CB_OK, cb_node_handle(&node, &nmt, 1U));
    TEST_ASSERT_EQUAL(CB_STOPPED, node.state);
    nmt.data[0] = 128U;
    TEST_ASSERT_EQUAL(CB_OK, cb_node_handle(&node, &nmt, 2U));
    TEST_ASSERT_EQUAL(CB_PRE_OPERATIONAL, node.state);
    nmt.data[0] = 7U;
    TEST_ASSERT_EQUAL(CB_ARG, cb_node_handle(&node, &nmt, 2U));
    nmt.dlc = 1U;
    TEST_ASSERT_EQUAL(CB_LENGTH, cb_node_handle(&node, &nmt, 2U));
}

static void test_reset_node_and_communication(void) {
    cb_frame reset = {0U, 2U, {130U, 1U}};
    node.objects[3].value = 80U;
    node.objects[2].value = 500U;
    TEST_ASSERT_EQUAL(CB_OK, cb_node_handle(&node, &reset, 20U));
    TEST_ASSERT_EQUAL(80U, node.objects[3].value);
    TEST_ASSERT_EQUAL(100U, node.objects[2].value);
    TEST_ASSERT_EQUAL(CB_PRE_OPERATIONAL, node.state);
    reset.data[0] = 129U;
    TEST_ASSERT_EQUAL(CB_OK, cb_node_handle(&node, &reset, 30U));
    TEST_ASSERT_EQUAL(0U, node.objects[3].value);
}

static void test_periodic_heartbeat_and_clock_wrap(void) {
    cb_frame frame;
    TEST_ASSERT_EQUAL(CB_OK, cb_node_tick(&node, 99U));
    TEST_ASSERT_EQUAL(CB_EMPTY, cb_memory_receive(&client, &frame));
    TEST_ASSERT_EQUAL(CB_OK, cb_node_tick(&node, 100U));
    TEST_ASSERT_EQUAL(CB_OK, cb_memory_receive(&client, &frame));
    TEST_ASSERT_EQUAL(127U, frame.data[0]);
    TEST_ASSERT_EQUAL(CB_TIME, cb_node_tick(&node, 99U));
    TEST_ASSERT_EQUAL(CB_TIME, cb_node_poll(&node, 99U));
    TEST_ASSERT_EQUAL(CB_OK, cb_node_init(&node, 1U, cb_memory_hal(&endpoint), UINT32_MAX - 50U));
    TEST_ASSERT_EQUAL(CB_OK, cb_node_start(&node));
    clear_output();
    TEST_ASSERT_EQUAL(CB_OK, cb_node_tick(&node, 49U));
    TEST_ASSERT_EQUAL(CB_OK, cb_memory_receive(&client, &frame));
}

static void test_sdo_upload_sizes_and_bytes(void) {
    const uint16_t indices[3] = {0x1001U, 0x1017U, 0x1000U};
    const uint8_t commands[3] = {0x4FU, 0x4BU, 0x43U};
    unsigned i;
    for (i = 0U; i < 3U; ++i) {
        cb_frame reply = response(sdo_request(0x40U, indices[i], 0U));
        TEST_ASSERT_EQUAL(commands[i], reply.data[0]);
        TEST_ASSERT_EQUAL_HEX16(indices[i], cb_get_le(reply.data + 1, 2U));
    }
}

static void test_sdo_download_upload_roundtrip(void) {
    cb_frame reply = response(sdo_request(0x2BU, 0x2001U, 999U));
    TEST_ASSERT_EQUAL_HEX8(0x60U, reply.data[0]);
    reply = response(sdo_request(0x40U, 0x2001U, 0U));
    TEST_ASSERT_EQUAL_UINT32(999U, cb_get_le(reply.data + 4, 2U));
}

static void test_sdo_abort_range_type_readonly_missing_subindex(void) {
    const uint8_t commands[4] = {0x2BU, 0x23U, 0x2BU, 0x40U};
    const uint16_t indices[4] = {0x2001U, 0x2001U, 0x2000U, 0x9999U};
    const uint32_t expected[4] = {CB_ABORT_RANGE, CB_ABORT_TYPE, CB_ABORT_READ_ONLY, CB_ABORT_NO_OBJECT};
    unsigned i;
    cb_frame request;
    cb_frame reply;
    for (i = 0U; i < 4U; ++i) {
        reply = response(sdo_request(commands[i], indices[i], 1001U));
        TEST_ASSERT_EQUAL_HEX8(0x80U, reply.data[0]);
        TEST_ASSERT_EQUAL_HEX32(expected[i], cb_get_le(reply.data + 4, 4U));
    }
    request = sdo_request(0x40U, 0x1001U, 0U); request.data[3] = 1U;
    reply = response(request);
    TEST_ASSERT_EQUAL_HEX32(CB_ABORT_SUBINDEX, cb_get_le(reply.data + 4, 4U));
    TEST_ASSERT_EQUAL(0U, node.objects[4].value);
}

static void test_sdo_malformed_and_client_abort(void) {
    cb_frame request = sdo_request(0x21U, 0x2001U, 1U);
    cb_frame reply = response(request);
    TEST_ASSERT_EQUAL_HEX32(CB_ABORT_COMMAND, cb_get_le(reply.data + 4, 4U));
    request.dlc = 3U;
    reply = response(request);
    TEST_ASSERT_EQUAL_HEX32(CB_ABORT_COMMAND, cb_get_le(reply.data + 4, 4U));
    request = sdo_request(0x80U, 0x2001U, 0U);
    TEST_ASSERT_EQUAL(CB_IGNORED, cb_node_handle(&node, &request, 0U));
    TEST_ASSERT_EQUAL(CB_EMPTY, cb_memory_receive(&client, &reply));
}

static void test_od_errors_and_state_guard(void) {
    uint32_t value = 0U;
    uint8_t size = 0U;
    TEST_ASSERT_NULL(cb_od_find(NULL, 1U));
    TEST_ASSERT_EQUAL_HEX32(CB_ABORT_COMMAND, cb_od_read(&node, 0x1001U, 0U, NULL, &size));
    TEST_ASSERT_EQUAL_HEX32(CB_ABORT_NO_OBJECT, cb_od_read(&node, 0U, 0U, &value, &size));
    TEST_ASSERT_EQUAL_HEX32(CB_ABORT_NO_OBJECT, cb_od_write(&node, 0U, 0U, 1U, 0U, 0U));
    TEST_ASSERT_EQUAL_HEX32(CB_ABORT_SUBINDEX, cb_od_write(&node, 0x2001U, 1U, 2U, 0U, 0U));
    TEST_ASSERT_EQUAL_HEX32(CB_ABORT_RANGE, cb_od_write(&node, 0x1017U, 0U, 2U, 9U, 0U));
    TEST_ASSERT_EQUAL_HEX32(CB_ABORT_STATE, cb_od_write(&node, 0x2002U, 0U, 1U, 1U, 0U));
    operational();
    TEST_ASSERT_EQUAL(0U, cb_od_write(&node, 0x2002U, 0U, 1U, 1U, 0U));
    TEST_ASSERT_EQUAL_HEX32(CB_ABORT_STATE, cb_od_write(&node, 0x2004U, 0U, 1U, 1U, 0U));
    TEST_ASSERT_EQUAL(0U, cb_od_write(&node, 0x2002U, 0U, 1U, 0U, 0U));
    node.objects[1].value = 1U;
    TEST_ASSERT_EQUAL_HEX32(CB_ABORT_STATE, cb_od_write(&node, 0x2002U, 0U, 1U, 1U, 0U));
    TEST_ASSERT_EQUAL(0U, cb_od_write(&node, 0x2004U, 0U, 1U, 0U, 0U));
    TEST_ASSERT_EQUAL(1U, node.objects[1].value);
    TEST_ASSERT_EQUAL(0U, cb_od_write(&node, 0x2004U, 0U, 1U, 1U, 0U));
    TEST_ASSERT_EQUAL(0U, node.objects[1].value);
}

static void test_mapping_pack_and_atomic_apply(void) {
    cb_frame frame = {0x201U, 3U, {0xF4U, 1U, 1U}};
    cb_frame out;
    operational();
    TEST_ASSERT_EQUAL(CB_OK, cb_pdo_apply(&node, &node.rpdo, &frame, 0U));
    TEST_ASSERT_EQUAL(500U, node.objects[4].value);
    TEST_ASSERT_EQUAL(1U, node.objects[5].value);
    TEST_ASSERT_EQUAL(CB_OK, cb_pdo_pack(&node, &node.tpdo, &out));
    TEST_ASSERT_EQUAL(4U, out.dlc);
    TEST_ASSERT_EQUAL(1U, out.data[2]);
    frame.data[0] = 100U; frame.data[1] = 0U; frame.data[2] = 2U;
    TEST_ASSERT_EQUAL(CB_RANGE, cb_pdo_apply(&node, &node.rpdo, &frame, 0U));
    TEST_ASSERT_EQUAL(500U, node.objects[4].value);
}

static void test_mapping_invalid_parameters_and_access(void) {
    cb_frame frame = {0x201U, 3U, {0}};
    cb_pdo_map map = node.rpdo;
    TEST_ASSERT_EQUAL(CB_ARG, cb_pdo_pack(NULL, &map, &frame));
    TEST_ASSERT_EQUAL(CB_ARG, cb_pdo_pack(&node, NULL, &frame));
    TEST_ASSERT_EQUAL(CB_STATE, cb_pdo_pack(&node, &node.tpdo, &frame));
    TEST_ASSERT_EQUAL(CB_STATE, cb_pdo_apply(&node, &map, &frame, 0U));
    operational();
    TEST_ASSERT_EQUAL(CB_ARG, cb_pdo_pack(&node, &node.tpdo, NULL));
    TEST_ASSERT_EQUAL(CB_ARG, cb_pdo_apply(&node, &map, NULL, 0U));
    map.count = 0U;
    TEST_ASSERT_EQUAL(CB_RANGE, cb_pdo_apply(&node, &map, &frame, 0U));
    map = node.rpdo; map.entries[0].index = 0x9999U;
    TEST_ASSERT_EQUAL(CB_NOT_FOUND, cb_pdo_apply(&node, &map, &frame, 0U));
    map = node.rpdo; map.entries[0].index = 0x1000U;
    TEST_ASSERT_EQUAL(CB_ACCESS, cb_pdo_apply(&node, &map, &frame, 0U));
    map = node.rpdo; map.entries[0].bits = 7U;
    TEST_ASSERT_EQUAL(CB_LENGTH, cb_pdo_apply(&node, &map, &frame, 0U));
    map = node.rpdo; map.entries[1] = map.entries[0];
    TEST_ASSERT_EQUAL(CB_ARG, cb_pdo_apply(&node, &map, &frame, 0U));
    map = node.rpdo; frame.id = 0x202U;
    TEST_ASSERT_EQUAL(CB_IGNORED, cb_pdo_apply(&node, &map, &frame, 0U));
    frame.id = 0x201U; frame.dlc = 2U;
    TEST_ASSERT_EQUAL(CB_LENGTH, cb_pdo_apply(&node, &map, &frame, 0U));
}

static void test_mapping_more_than_eight_bytes(void) {
    cb_frame frame;
    cb_pdo_map map = {1U, 3U, {{0x1000U, 0U, 32U}, {0x1017U, 0U, 16U}, {0x2000U, 0U, 32U}, {0}}};
    node.objects[0].access |= CB_OD_TPDO;
    node.objects[2].access |= CB_OD_TPDO;
    node.objects[3].size = 4U;
    operational();
    TEST_ASSERT_EQUAL(CB_LENGTH, cb_pdo_pack(&node, &map, &frame));
}

static void test_lifter_position_and_target_stop(void) {
    cb_frame frame = command(5U, 1U, 0U);
    operational();
    TEST_ASSERT_EQUAL(CB_OK, cb_node_handle(&node, &frame, 0U));
    TEST_ASSERT_EQUAL(CB_OK, cb_node_tick(&node, 20U));
    TEST_ASSERT_EQUAL(2U, node.objects[3].value);
    TEST_ASSERT_EQUAL(CB_OK, cb_node_tick(&node, 50U));
    TEST_ASSERT_EQUAL(5U, node.objects[3].value);
    TEST_ASSERT_EQUAL(0U, node.objects[5].value);
    frame = command(1U, 1U, 1U);
    TEST_ASSERT_EQUAL(CB_OK, cb_node_handle(&node, &frame, 50U));
    TEST_ASSERT_EQUAL(CB_OK, cb_node_tick(&node, 90U));
    TEST_ASSERT_EQUAL(1U, node.objects[3].value);
    TEST_ASSERT_EQUAL(0U, node.objects[5].value);
}

static void test_watchdog_stops_and_latches_error(void) {
    cb_frame frame = command(1000U, 1U, 0U);
    operational();
    TEST_ASSERT_EQUAL(CB_OK, cb_node_handle(&node, &frame, 0U));
    TEST_ASSERT_EQUAL(CB_OK, cb_node_tick(&node, 250U));
    TEST_ASSERT_EQUAL(0U, node.objects[5].value);
    TEST_ASSERT_EQUAL(0x10U, node.objects[1].value);
    TEST_ASSERT_EQUAL(0U, node.objects[3].value);
}

static void test_checksum_and_length_faults(void) {
    cb_frame frame = command(500U, 1U, 0U);
    operational();
    frame.data[4] ^= 1U;
    TEST_ASSERT_EQUAL(CB_RANGE, cb_node_handle(&node, &frame, 0U));
    TEST_ASSERT_EQUAL(1U, node.objects[1].value);
    TEST_ASSERT_EQUAL(0U, node.objects[4].value);
    frame.dlc = 4U;
    TEST_ASSERT_EQUAL(CB_LENGTH, cb_node_handle(&node, &frame, 0U));
}

static void test_sequence_gap_replay_and_wrap(void) {
    cb_frame frame = command(500U, 1U, 255U);
    operational();
    TEST_ASSERT_EQUAL(CB_OK, cb_node_handle(&node, &frame, 0U));
    frame = command(500U, 1U, 0U);
    TEST_ASSERT_EQUAL(CB_OK, cb_node_handle(&node, &frame, 1U));
    TEST_ASSERT_EQUAL(CB_RANGE, cb_node_handle(&node, &frame, 2U));
    TEST_ASSERT_EQUAL(0x10U, node.objects[1].value);
    TEST_ASSERT_EQUAL(0U, node.objects[5].value);
}

static void test_out_of_range_pdo_is_atomic(void) {
    cb_frame frame = command(1001U, 1U, 0U);
    operational();
    TEST_ASSERT_EQUAL(CB_RANGE, cb_node_handle(&node, &frame, 0U));
    TEST_ASSERT_EQUAL(0U, node.objects[4].value);
    TEST_ASSERT_EQUAL(0U, node.objects[5].value);
}

static void test_stopped_and_preop_ignore_pdo(void) {
    cb_frame frame = command(5U, 1U, 0U);
    TEST_ASSERT_EQUAL(CB_IGNORED, cb_node_handle(&node, &frame, 0U));
    node.state = CB_STOPPED;
    frame = sdo_request(0x40U, 0x1001U, 0U);
    TEST_ASSERT_EQUAL(CB_IGNORED, cb_node_handle(&node, &frame, 0U));
    node.state = CB_PRE_OPERATIONAL;
    frame.id = 0x555U;
    TEST_ASSERT_EQUAL(CB_IGNORED, cb_node_handle(&node, &frame, 0U));
    frame.id = 0xFFFU;
    TEST_ASSERT_EQUAL(CB_RANGE, cb_node_handle(&node, &frame, 0U));
}

static cb_result fail_send(void *context, const cb_frame *frame) { (void)context; (void)frame; return CB_IO; }
static cb_result fail_receive(void *context, cb_frame *frame) { (void)context; (void)frame; return CB_IO; }

static void test_hal_errors_are_propagated(void) {
    node.hal.send = fail_send;
    TEST_ASSERT_EQUAL(CB_IO, cb_node_tick(&node, 100U));
    node.state = CB_INITIALISING;
    TEST_ASSERT_EQUAL(CB_IO, cb_node_start(&node));
    node.state = CB_OPERATIONAL;
    TEST_ASSERT_EQUAL(CB_IO, cb_node_tpdo(&node));
    node.hal.receive = fail_receive;
    TEST_ASSERT_EQUAL(CB_IO, cb_node_poll(&node, 100U));
}

static void test_poll_processes_queue_and_reports_bad_input(void) {
    cb_frame nmt = {0U, 2U, {1U, 1U}};
    cb_frame bad = {0U, 1U, {1U}};
    TEST_ASSERT_EQUAL(CB_OK, cb_memory_send(&client, &bad));
    TEST_ASSERT_EQUAL(CB_OK, cb_memory_send(&client, &nmt));
    TEST_ASSERT_EQUAL(CB_LENGTH, cb_node_poll(&node, 0U));
    TEST_ASSERT_EQUAL(CB_OPERATIONAL, node.state);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_frame_roundtrip_and_padding);
    RUN_TEST(test_frame_boundaries);
    RUN_TEST(test_frame_invalid_arguments);
    RUN_TEST(test_memory_fifo_wrap_and_full);
    RUN_TEST(test_memory_invalid_arguments);
    RUN_TEST(test_initial_state_and_bootup);
    RUN_TEST(test_node_invalid_arguments);
    RUN_TEST(test_nmt_target_broadcast_and_states);
    RUN_TEST(test_reset_node_and_communication);
    RUN_TEST(test_periodic_heartbeat_and_clock_wrap);
    RUN_TEST(test_sdo_upload_sizes_and_bytes);
    RUN_TEST(test_sdo_download_upload_roundtrip);
    RUN_TEST(test_sdo_abort_range_type_readonly_missing_subindex);
    RUN_TEST(test_sdo_malformed_and_client_abort);
    RUN_TEST(test_od_errors_and_state_guard);
    RUN_TEST(test_mapping_pack_and_atomic_apply);
    RUN_TEST(test_mapping_invalid_parameters_and_access);
    RUN_TEST(test_mapping_more_than_eight_bytes);
    RUN_TEST(test_lifter_position_and_target_stop);
    RUN_TEST(test_watchdog_stops_and_latches_error);
    RUN_TEST(test_checksum_and_length_faults);
    RUN_TEST(test_sequence_gap_replay_and_wrap);
    RUN_TEST(test_out_of_range_pdo_is_atomic);
    RUN_TEST(test_stopped_and_preop_ignore_pdo);
    RUN_TEST(test_hal_errors_are_propagated);
    RUN_TEST(test_poll_processes_queue_and_reports_bad_input);
    return UNITY_END();
}
