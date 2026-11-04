#ifndef CANLAB_H
#define CANLAB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CB_OK = 0, CB_IGNORED = 1, CB_ARG = -1, CB_RANGE = -2,
    CB_LENGTH = -3, CB_EMPTY = -4, CB_FULL = -5, CB_STATE = -6,
    CB_IO = -7, CB_NOT_FOUND = -8, CB_ACCESS = -9, CB_TIME = -10
} cb_result;

typedef struct { uint16_t id; uint8_t dlc; uint8_t data[8]; } cb_frame;
#define CB_WIRE_SIZE 11U
cb_result cb_frame_valid(const cb_frame *frame);
cb_result cb_frame_encode(const cb_frame *frame, uint8_t *buffer, size_t length);
cb_result cb_frame_decode(cb_frame *frame, const uint8_t *buffer, size_t length);

typedef cb_result (*cb_send_fn)(void *context, const cb_frame *frame);
typedef cb_result (*cb_receive_fn)(void *context, cb_frame *frame);
typedef struct { void *context; cb_send_fn send; cb_receive_fn receive; } cb_hal;

#define CB_QUEUE_CAPACITY 32U
typedef struct {
    cb_frame frames[2][CB_QUEUE_CAPACITY];
    size_t head[2];
    size_t count[2];
} cb_memory_bus;
typedef struct { cb_memory_bus *bus; unsigned side; } cb_memory_endpoint;
void cb_memory_init(cb_memory_bus *bus);
cb_hal cb_memory_hal(cb_memory_endpoint *endpoint);
cb_result cb_memory_send(void *context, const cb_frame *frame);
cb_result cb_memory_receive(void *context, cb_frame *frame);

typedef enum { CB_INITIALISING = 0, CB_STOPPED = 4, CB_OPERATIONAL = 5, CB_PRE_OPERATIONAL = 127 } cb_nmt;
#define CB_OD_COUNT 8U
#define CB_OD_READ 1U
#define CB_OD_WRITE 2U
#define CB_OD_TPDO 4U
#define CB_OD_RPDO 8U
typedef struct {
    uint16_t index;
    uint8_t size;
    uint8_t access;
    uint32_t value;
    uint32_t minimum;
    uint32_t maximum;
} cb_object;
typedef struct { uint16_t index; uint8_t subindex; uint8_t bits; } cb_mapping;
typedef struct { uint16_t cob_id; uint8_t count; cb_mapping entries[4]; } cb_pdo_map;
typedef struct {
    uint8_t node_id;
    cb_nmt state;
    cb_hal hal;
    cb_object objects[CB_OD_COUNT];
    cb_pdo_map rpdo;
    cb_pdo_map tpdo;
    uint32_t last_tick;
    uint32_t last_heartbeat;
    uint32_t last_motion;
    uint32_t last_command;
    uint8_t last_sequence;
    bool sequence_valid;
} cb_node;

#define CB_ABORT_COMMAND UINT32_C(0x05040001)
#define CB_ABORT_READ_ONLY UINT32_C(0x06010002)
#define CB_ABORT_NO_OBJECT UINT32_C(0x06020000)
#define CB_ABORT_TYPE UINT32_C(0x06070010)
#define CB_ABORT_SUBINDEX UINT32_C(0x06090011)
#define CB_ABORT_RANGE UINT32_C(0x06090030)
#define CB_ABORT_STATE UINT32_C(0x08000022)

cb_result cb_node_init(cb_node *node, uint8_t id, cb_hal hal, uint32_t now_ms);
cb_result cb_node_start(cb_node *node);
cb_result cb_node_handle(cb_node *node, const cb_frame *frame, uint32_t now_ms);
cb_result cb_node_tick(cb_node *node, uint32_t now_ms);
cb_result cb_node_poll(cb_node *node, uint32_t now_ms);
cb_result cb_node_tpdo(cb_node *node);
cb_object *cb_od_find(cb_node *node, uint16_t index);
uint32_t cb_od_read(cb_node *node, uint16_t index, uint8_t subindex, uint32_t *value, uint8_t *size);
uint32_t cb_od_check_write(cb_node *node, uint16_t index, uint8_t subindex, uint8_t size, uint32_t value);
uint32_t cb_od_write(cb_node *node, uint16_t index, uint8_t subindex, uint8_t size, uint32_t value, uint32_t now_ms);
cb_result cb_pdo_pack(cb_node *node, const cb_pdo_map *map, cb_frame *frame);
cb_result cb_pdo_apply(cb_node *node, const cb_pdo_map *map, const cb_frame *frame, uint32_t now_ms);
uint32_t cb_get_le(const uint8_t *bytes, uint8_t size);
void cb_put_le(uint8_t *bytes, uint32_t value, uint8_t size);

#endif
