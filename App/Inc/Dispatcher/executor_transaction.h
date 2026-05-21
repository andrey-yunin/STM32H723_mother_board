#ifndef INC_DISPATCHER_EXECUTOR_TRANSACTION_H_
#define INC_DISPATCHER_EXECUTOR_TRANSACTION_H_

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "can_response_router.h"

typedef enum {
    EXECUTOR_TRANSACTION_RESPONSE_DONE_ONLY = 0,
    EXECUTOR_TRANSACTION_RESPONSE_DATA_THEN_DONE,
    EXECUTOR_TRANSACTION_RESPONSE_MULTI_DATA_THEN_DONE
} ExecutorTransactionResponsePolicy_t;

typedef enum {
    EXECUTOR_TRANSACTION_STATE_SENT = 1,
    EXECUTOR_TRANSACTION_STATE_ACKED,
    EXECUTOR_TRANSACTION_STATE_DATA_SEEN,
    EXECUTOR_TRANSACTION_STATE_DONE,
    EXECUTOR_TRANSACTION_STATE_NACK,
    EXECUTOR_TRANSACTION_STATE_ACK_TIMEOUT,
    EXECUTOR_TRANSACTION_STATE_OPERATION_TIMEOUT
} ExecutorTransactionState_t;

typedef enum {
    EXECUTOR_TRANSACTION_EVENT_NONE = 0,
    EXECUTOR_TRANSACTION_EVENT_ACKED,
    EXECUTOR_TRANSACTION_EVENT_DATA_ACCEPTED,
    EXECUTOR_TRANSACTION_EVENT_DONE,
    EXECUTOR_TRANSACTION_EVENT_NACK,
    EXECUTOR_TRANSACTION_EVENT_PROTOCOL_ERROR,
    EXECUTOR_TRANSACTION_EVENT_ACK_TIMEOUT,
    EXECUTOR_TRANSACTION_EVENT_OPERATION_TIMEOUT
} ExecutorTransactionEvent_t;

typedef enum {
    EXECUTOR_TRANSACTION_REGISTER_OK = 0,
    EXECUTOR_TRANSACTION_REGISTER_INVALID_ARG,
    EXECUTOR_TRANSACTION_REGISTER_DUPLICATE,
    EXECUTOR_TRANSACTION_REGISTER_TABLE_FULL,
    EXECUTOR_TRANSACTION_REGISTER_ROUTE_FAILED
} ExecutorTransactionRegisterStatus_t;

typedef struct {
    bool active;
    uint32_t operation_id;
    uint16_t host_command_code;
    uint16_t low_command_code;
    uint8_t expected_node_id;
    uint8_t expected_channel;
    bool channel_valid;
    ExecutorTransactionResponsePolicy_t response_policy;
    ExecutorTransactionState_t state;
    uint32_t sent_time_ms;
    uint32_t ack_timeout_ms;
    uint32_t operation_timeout_ms;
    uint16_t last_error_code;
    uint8_t data_count;
} ExecutorTransaction_t;

typedef struct {
    ExecutorTransaction_t entries[JOB_MAX_EXECUTOR_TRANSACTIONS];
} ExecutorTransactionTable_t;

typedef struct {
    CanTxOwner_t route_owner;
    uint32_t operation_id;
    uint16_t host_command_code;
    uint16_t low_command_code;
    uint8_t expected_node_id;
    uint8_t expected_channel;
    bool channel_valid;
    ExecutorTransactionResponsePolicy_t response_policy;
    uint32_t operation_timeout_ms;
    uint32_t sent_time_ms;
} ExecutorTransactionStart_t;

typedef struct {
    bool matched;
    ExecutorTransactionEvent_t event;
    uint32_t operation_id;
    uint16_t host_command_code;
    uint16_t low_command_code;
    uint8_t node_id;
    uint8_t channel;
    bool channel_valid;
    uint16_t error_code;
    const char* reason;
} ExecutorTransactionUpdate_t;

void ExecutorTransactionTable_Reset(ExecutorTransactionTable_t* table);
void ExecutorTransactionTable_ResetOperation(
        ExecutorTransactionTable_t* table,
        CanTxOwner_t route_owner,
        uint32_t operation_id);

bool ExecutorTransactionTable_Register(
        ExecutorTransactionTable_t* table,
        const ExecutorTransactionStart_t* start);

ExecutorTransactionRegisterStatus_t ExecutorTransactionTable_RegisterEx(
        ExecutorTransactionTable_t* table,
        const ExecutorTransactionStart_t* start);

bool ExecutorTransactionTable_HasMatching(
        ExecutorTransactionTable_t* table,
        const CanRoutedResponse_t* routed,
        bool require_channel);

ExecutorTransactionUpdate_t ExecutorTransactionTable_HandleAck(
        ExecutorTransactionTable_t* table,
        const CanRoutedResponse_t* routed);

ExecutorTransactionUpdate_t ExecutorTransactionTable_HandleNack(
        ExecutorTransactionTable_t* table,
        const CanRoutedResponse_t* routed);

ExecutorTransactionUpdate_t ExecutorTransactionTable_HandleData(
        ExecutorTransactionTable_t* table,
        const CanRoutedResponse_t* routed);

ExecutorTransactionUpdate_t ExecutorTransactionTable_HandleDone(
        ExecutorTransactionTable_t* table,
        const CanRoutedResponse_t* routed);

bool ExecutorTransactionTable_CheckTimeouts(
        ExecutorTransactionTable_t* table,
        uint32_t now_ms,
        ExecutorTransactionUpdate_t* out_update);

#endif /* INC_DISPATCHER_EXECUTOR_TRANSACTION_H_ */
