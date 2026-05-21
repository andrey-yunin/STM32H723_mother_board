/*
 * executor_command_tx.c
 *
 * Outbound Executor CAN command boundary.
 */

#include "Dispatcher/executor_command_tx.h"
#include "shared_resources.h"
#include "main.h"
#include "task.h"

static uint16_t ExecutorCommandTx_ReadU16Le(const uint8_t* src)
{
    return (uint16_t)(src[0] | ((uint16_t)src[1] << 8));
}

static bool ExecutorCommandTx_IsValidCommand(
        const ExecutorCommandTxCommand_t* command)
{
    if (command == NULL) {
        return false;
    }

    const CAN_Message_t* frame = &command->frame;
    const ExecutorTransactionStart_t* tx = &command->transaction;

    if (!frame->is_extended ||
            frame->dlc != CAN_PAYLOAD_SIZE ||
            CAN_GET_MSG_TYPE(frame->id) != CAN_MSG_TYPE_COMMAND ||
            CAN_GET_SRC_ADDR(frame->id) != CAN_ADDR_CONDUCTOR ||
            CAN_GET_DST_ADDR(frame->id) != tx->expected_node_id) {
        return false;
    }

    if (ExecutorCommandTx_ReadU16Le(&frame->data[0]) != tx->low_command_code) {
        return false;
    }

    if (tx->channel_valid && frame->data[2] != tx->expected_channel) {
        return false;
    }

    return true;
}

static ExecutorCommandTxStatus_t ExecutorCommandTx_MapRegisterStatus(
        ExecutorTransactionRegisterStatus_t status)
{
    switch (status) {
        case EXECUTOR_TRANSACTION_REGISTER_OK:
            return EXECUTOR_COMMAND_TX_OK;

        case EXECUTOR_TRANSACTION_REGISTER_DUPLICATE:
            return EXECUTOR_COMMAND_TX_DUPLICATE_TRANSACTION;

        case EXECUTOR_TRANSACTION_REGISTER_INVALID_ARG:
            return EXECUTOR_COMMAND_TX_INVALID_ARG;

        case EXECUTOR_TRANSACTION_REGISTER_TABLE_FULL:
        case EXECUTOR_TRANSACTION_REGISTER_ROUTE_FAILED:
        default:
            return EXECUTOR_COMMAND_TX_ROUTE_REGISTRATION_FAILED;
    }
}

ExecutorCommandTxStatus_t ExecutorCommandTx_SubmitBatch(
        ExecutorTransactionTable_t* transaction_table,
        const ExecutorCommandTxCommand_t* commands,
        uint8_t command_count)
{
    if (transaction_table == NULL || (commands == NULL && command_count > 0U)) {
        return EXECUTOR_COMMAND_TX_INVALID_ARG;
    }

    if (command_count == 0U) {
        return EXECUTOR_COMMAND_TX_OK;
    }

    for (uint8_t i = 0; i < command_count; i++) {
        if (!ExecutorCommandTx_IsValidCommand(&commands[i])) {
            return EXECUTOR_COMMAND_TX_INVALID_FRAME;
        }
    }

    if (can_tx_queue_handle == NULL) {
        return EXECUTOR_COMMAND_TX_QUEUE_SEND_FAILED;
    }

    const uint32_t sent_time_ms = HAL_GetTick();

    for (uint8_t i = 0; i < command_count; i++) {
        ExecutorTransactionStart_t tx_start = commands[i].transaction;
        tx_start.sent_time_ms = sent_time_ms;

        ExecutorTransactionRegisterStatus_t register_status =
                ExecutorTransactionTable_RegisterEx(transaction_table, &tx_start);
        if (register_status != EXECUTOR_TRANSACTION_REGISTER_OK) {
            ExecutorTransactionTable_ResetOperation(
                    transaction_table,
                    tx_start.route_owner,
                    tx_start.operation_id);
            return ExecutorCommandTx_MapRegisterStatus(register_status);
        }
    }

    ExecutorCommandTxStatus_t enqueue_status = EXECUTOR_COMMAND_TX_OK;

    vTaskSuspendAll();
    if (uxQueueSpacesAvailable(can_tx_queue_handle) < command_count) {
        enqueue_status = EXECUTOR_COMMAND_TX_QUEUE_FULL;
    }
    else {
        for (uint8_t i = 0; i < command_count; i++) {
            if (xQueueSend(can_tx_queue_handle, &commands[i].frame, 0) != pdPASS) {
                enqueue_status = EXECUTOR_COMMAND_TX_QUEUE_SEND_FAILED;
                break;
            }
        }
    }
    (void)xTaskResumeAll();

    if (enqueue_status != EXECUTOR_COMMAND_TX_OK) {
        ExecutorTransactionTable_ResetOperation(
                transaction_table,
                commands[0].transaction.route_owner,
                commands[0].transaction.operation_id);
        return enqueue_status;
    }

    return EXECUTOR_COMMAND_TX_OK;
}

const char* ExecutorCommandTx_StatusString(ExecutorCommandTxStatus_t status)
{
    switch (status) {
        case EXECUTOR_COMMAND_TX_OK:
            return "OK";
        case EXECUTOR_COMMAND_TX_INVALID_ARG:
            return "INVALID_ARG";
        case EXECUTOR_COMMAND_TX_INVALID_FRAME:
            return "INVALID_FRAME";
        case EXECUTOR_COMMAND_TX_DUPLICATE_TRANSACTION:
            return "DUPLICATE_TRANSACTION";
        case EXECUTOR_COMMAND_TX_ROUTE_REGISTRATION_FAILED:
            return "ROUTE_REGISTRATION_FAILED";
        case EXECUTOR_COMMAND_TX_QUEUE_FULL:
            return "QUEUE_FULL";
        case EXECUTOR_COMMAND_TX_QUEUE_SEND_FAILED:
            return "QUEUE_SEND_FAILED";
        default:
            return "UNKNOWN";
    }
}
