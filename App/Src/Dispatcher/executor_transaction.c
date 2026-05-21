/*
 * executor_transaction.c
 *
 * Common low-level executor transaction state machine.
 */

#include "executor_transaction.h"
#include <string.h>

static ExecutorTransactionUpdate_t ExecutorTransaction_MakeUpdate(
        ExecutorTransactionEvent_t event,
        const char* reason)
{
    ExecutorTransactionUpdate_t update;
    memset(&update, 0, sizeof(update));
    update.event = event;
    update.reason = reason;
    return update;
}

static void ExecutorTransaction_FillUpdate(
        ExecutorTransactionUpdate_t* update,
        const ExecutorTransaction_t* tx)
{
    if (update == NULL || tx == NULL) {
        return;
    }

    update->matched = true;
    update->operation_id = tx->operation_id;
    update->host_command_code = tx->host_command_code;
    update->low_command_code = tx->low_command_code;
    update->node_id = tx->expected_node_id;
    update->channel = tx->expected_channel;
    update->channel_valid = tx->channel_valid;
    update->error_code = tx->last_error_code;
}

static bool ExecutorTransaction_IsTerminalState(ExecutorTransactionState_t state)
{
    return state == EXECUTOR_TRANSACTION_STATE_DONE ||
            state == EXECUTOR_TRANSACTION_STATE_NACK ||
            state == EXECUTOR_TRANSACTION_STATE_ACK_TIMEOUT ||
            state == EXECUTOR_TRANSACTION_STATE_OPERATION_TIMEOUT;
}

static bool ExecutorTransaction_TickElapsed(
        uint32_t now_ms,
        uint32_t start_ms,
        uint32_t timeout_ms)
{
    return (uint32_t)(now_ms - start_ms) >= timeout_ms;
}

static bool ExecutorTransaction_CommandLevelMatches(
        const ExecutorTransaction_t* tx,
        const CanRoutedResponse_t* routed)
{
    if (tx == NULL || routed == NULL || !routed->context_valid) {
        return false;
    }

    const CAN_Response_t* response = &routed->parsed;

    return tx->active &&
            !ExecutorTransaction_IsTerminalState(tx->state) &&
            tx->expected_node_id == response->source_addr &&
            tx->low_command_code == routed->context_command_code;
}

static bool ExecutorTransaction_HasDuplicateActive(
        const ExecutorTransactionTable_t* table,
        uint16_t low_command_code,
        uint8_t expected_node_id,
        uint8_t expected_channel,
        bool channel_valid)
{
    if (table == NULL) {
        return false;
    }

    for (uint8_t i = 0; i < JOB_MAX_EXECUTOR_TRANSACTIONS; i++) {
        const ExecutorTransaction_t* tx = &table->entries[i];

        if (!tx->active || ExecutorTransaction_IsTerminalState(tx->state)) {
            continue;
        }

        if (tx->expected_node_id != expected_node_id ||
                tx->low_command_code != low_command_code) {
            continue;
        }

        if (!tx->channel_valid || !channel_valid) {
            return true;
        }

        if (tx->expected_channel == expected_channel) {
            return true;
        }
    }

    return false;
}

static ExecutorTransaction_t* ExecutorTransaction_FindMatching(
        ExecutorTransactionTable_t* table,
        const CanRoutedResponse_t* routed,
        bool require_channel,
        bool* out_ambiguous)
{
    if (out_ambiguous != NULL) {
        *out_ambiguous = false;
    }

    if (table == NULL || routed == NULL || !routed->context_valid) {
        return NULL;
    }

    const CAN_Response_t* response = &routed->parsed;
    ExecutorTransaction_t* found = NULL;
    bool check_channel = require_channel || routed->context_channel_valid;
    uint8_t response_channel = routed->context_channel_valid
            ? routed->context_channel
            : response->ch_idx;

    for (uint8_t i = 0; i < JOB_MAX_EXECUTOR_TRANSACTIONS; i++) {
        ExecutorTransaction_t* tx = &table->entries[i];

        if (!tx->active || ExecutorTransaction_IsTerminalState(tx->state)) {
            continue;
        }

        if (tx->expected_node_id != response->source_addr ||
                tx->low_command_code != routed->context_command_code) {
            continue;
        }

        if (check_channel && tx->channel_valid &&
                tx->expected_channel != response_channel) {
            continue;
        }

        if (found != NULL) {
            if (out_ambiguous != NULL) {
                *out_ambiguous = true;
            }
            return NULL;
        }

        found = tx;
    }

    return found;
}

void ExecutorTransactionTable_Reset(ExecutorTransactionTable_t* table)
{
    if (table == NULL) {
        return;
    }

    memset(table, 0, sizeof(*table));
}

void ExecutorTransactionTable_ResetOperation(
        ExecutorTransactionTable_t* table,
        CanTxOwner_t route_owner,
        uint32_t operation_id)
{
    if (table == NULL) {
        return;
    }

    if (operation_id == 0U) {
        ExecutorTransactionTable_Reset(table);
        return;
    }

    CanResponseRouter_CloseOperation(route_owner, operation_id);

    for (uint8_t i = 0; i < JOB_MAX_EXECUTOR_TRANSACTIONS; i++) {
        ExecutorTransaction_t* tx = &table->entries[i];

        if (tx->active && tx->operation_id == operation_id) {
            memset(tx, 0, sizeof(*tx));
        }
    }
}

ExecutorTransactionRegisterStatus_t ExecutorTransactionTable_RegisterEx(
        ExecutorTransactionTable_t* table,
        const ExecutorTransactionStart_t* start)
{
    if (table == NULL || start == NULL) {
        return EXECUTOR_TRANSACTION_REGISTER_INVALID_ARG;
    }

    /*
     * Broadcast service requests are answered by real source NodeIDs, not by
     * NodeID 0x00. The router handles this case without a pre-created route,
     * so the transaction table must not keep an orphan broadcast transaction.
     */
    if (start->route_owner == TX_OWNER_SERVICE_INTERNAL &&
            start->expected_node_id == CAN_ADDR_BROADCAST &&
            !start->channel_valid) {
        return CanResponseRouter_Register(
                start->route_owner,
                start->expected_node_id,
                start->low_command_code,
                start->expected_channel,
                start->channel_valid,
                start->operation_id,
                start->host_command_code)
                    ? EXECUTOR_TRANSACTION_REGISTER_OK
                    : EXECUTOR_TRANSACTION_REGISTER_ROUTE_FAILED;
    }

    if (ExecutorTransaction_HasDuplicateActive(
            table,
            start->low_command_code,
            start->expected_node_id,
            start->expected_channel,
            start->channel_valid)) {
        return EXECUTOR_TRANSACTION_REGISTER_DUPLICATE;
    }

    for (uint8_t i = 0; i < JOB_MAX_EXECUTOR_TRANSACTIONS; i++) {
        ExecutorTransaction_t* tx = &table->entries[i];

        if (!tx->active) {
            memset(tx, 0, sizeof(*tx));

            tx->active = true;
            tx->operation_id = start->operation_id;
            tx->host_command_code = start->host_command_code;
            tx->low_command_code = start->low_command_code;
            tx->expected_node_id = start->expected_node_id;
            tx->expected_channel = start->expected_channel;
            tx->channel_valid = start->channel_valid;
            tx->response_policy = start->response_policy;
            tx->state = EXECUTOR_TRANSACTION_STATE_SENT;
            tx->sent_time_ms = start->sent_time_ms;
            tx->ack_timeout_ms = JOB_ACK_TIMEOUT_MS;
            tx->operation_timeout_ms = start->operation_timeout_ms;

            if (!CanResponseRouter_Register(
                    start->route_owner,
                    start->expected_node_id,
                    start->low_command_code,
                    start->expected_channel,
                    start->channel_valid,
                    start->operation_id,
                    start->host_command_code)) {
                memset(tx, 0, sizeof(*tx));
                return EXECUTOR_TRANSACTION_REGISTER_ROUTE_FAILED;
            }

            return EXECUTOR_TRANSACTION_REGISTER_OK;
        }
    }

    return EXECUTOR_TRANSACTION_REGISTER_TABLE_FULL;
}

bool ExecutorTransactionTable_Register(
        ExecutorTransactionTable_t* table,
        const ExecutorTransactionStart_t* start)
{
    return ExecutorTransactionTable_RegisterEx(table, start) ==
            EXECUTOR_TRANSACTION_REGISTER_OK;
}

bool ExecutorTransactionTable_HasMatching(
        ExecutorTransactionTable_t* table,
        const CanRoutedResponse_t* routed,
        bool require_channel)
{
    return ExecutorTransaction_FindMatching(
            table,
            routed,
            require_channel,
            NULL) != NULL;
}

ExecutorTransactionUpdate_t ExecutorTransactionTable_HandleAck(
        ExecutorTransactionTable_t* table,
        const CanRoutedResponse_t* routed)
{
    ExecutorTransactionUpdate_t update =
            ExecutorTransaction_MakeUpdate(
                    EXECUTOR_TRANSACTION_EVENT_NONE,
                    "no matching transaction for ACK");

    if (table == NULL || routed == NULL || !routed->context_valid) {
        update.reason = "invalid context for ACK";
        return update;
    }

    for (uint8_t i = 0; i < JOB_MAX_EXECUTOR_TRANSACTIONS; i++) {
        ExecutorTransaction_t* tx = &table->entries[i];

        if (!ExecutorTransaction_CommandLevelMatches(tx, routed)) {
            continue;
        }

        if (!update.matched) {
            ExecutorTransaction_FillUpdate(&update, tx);
            update.event = EXECUTOR_TRANSACTION_EVENT_ACKED;
            update.reason = NULL;
        }

        if (tx->state == EXECUTOR_TRANSACTION_STATE_SENT) {
            tx->state = EXECUTOR_TRANSACTION_STATE_ACKED;
        }
    }

    return update;
}

ExecutorTransactionUpdate_t ExecutorTransactionTable_HandleNack(
        ExecutorTransactionTable_t* table,
        const CanRoutedResponse_t* routed)
{
    ExecutorTransactionUpdate_t update =
            ExecutorTransaction_MakeUpdate(
                    EXECUTOR_TRANSACTION_EVENT_NONE,
                    "no matching transaction for NACK");

    if (table == NULL || routed == NULL || !routed->context_valid) {
        update.reason = "invalid context for NACK";
        return update;
    }

    const CAN_Response_t* response = &routed->parsed;

    for (uint8_t i = 0; i < JOB_MAX_EXECUTOR_TRANSACTIONS; i++) {
        ExecutorTransaction_t* tx = &table->entries[i];

        if (!ExecutorTransaction_CommandLevelMatches(tx, routed)) {
            continue;
        }

        tx->state = EXECUTOR_TRANSACTION_STATE_NACK;
        tx->last_error_code = response->error_code;
        tx->active = false;

        if (!update.matched) {
            ExecutorTransaction_FillUpdate(&update, tx);
            update.event = EXECUTOR_TRANSACTION_EVENT_NACK;
            update.error_code = response->error_code;
            update.reason = NULL;
        }
    }

    return update;
}

ExecutorTransactionUpdate_t ExecutorTransactionTable_HandleData(
        ExecutorTransactionTable_t* table,
        const CanRoutedResponse_t* routed)
{
    bool ambiguous = false;
    ExecutorTransaction_t* tx = ExecutorTransaction_FindMatching(
            table,
            routed,
            routed != NULL && routed->context_channel_valid,
            &ambiguous);

    if (tx == NULL) {
        return ExecutorTransaction_MakeUpdate(
                EXECUTOR_TRANSACTION_EVENT_NONE,
                ambiguous
                        ? "ambiguous transaction for DATA"
                        : "no matching transaction for DATA");
    }

    ExecutorTransactionUpdate_t update =
            ExecutorTransaction_MakeUpdate(
                    EXECUTOR_TRANSACTION_EVENT_DATA_ACCEPTED,
                    NULL);
    ExecutorTransaction_FillUpdate(&update, tx);

    if (tx->state == EXECUTOR_TRANSACTION_STATE_SENT) {
        tx->state = EXECUTOR_TRANSACTION_STATE_ACKED;
    }

    if (tx->state != EXECUTOR_TRANSACTION_STATE_ACKED &&
            tx->state != EXECUTOR_TRANSACTION_STATE_DATA_SEEN) {
        update.event = EXECUTOR_TRANSACTION_EVENT_PROTOCOL_ERROR;
        update.reason = "DATA in invalid transaction state";
        return update;
    }

    if (tx->response_policy == EXECUTOR_TRANSACTION_RESPONSE_DONE_ONLY) {
        update.event = EXECUTOR_TRANSACTION_EVENT_PROTOCOL_ERROR;
        update.reason = "DATA for DONE_ONLY transaction";
        return update;
    }

    if (tx->response_policy == EXECUTOR_TRANSACTION_RESPONSE_DATA_THEN_DONE &&
            tx->data_count > 0U) {
        update.event = EXECUTOR_TRANSACTION_EVENT_PROTOCOL_ERROR;
        update.reason = "extra DATA for DATA_THEN_DONE transaction";
        return update;
    }

    tx->data_count++;
    tx->state = EXECUTOR_TRANSACTION_STATE_DATA_SEEN;
    return update;
}

ExecutorTransactionUpdate_t ExecutorTransactionTable_HandleDone(
        ExecutorTransactionTable_t* table,
        const CanRoutedResponse_t* routed)
{
    bool ambiguous = false;
    ExecutorTransaction_t* tx = ExecutorTransaction_FindMatching(
            table,
            routed,
            true,
            &ambiguous);

    if (tx == NULL) {
        return ExecutorTransaction_MakeUpdate(
                EXECUTOR_TRANSACTION_EVENT_NONE,
                ambiguous
                        ? "ambiguous transaction for DONE"
                        : "no matching transaction for DONE");
    }

    ExecutorTransactionUpdate_t update =
            ExecutorTransaction_MakeUpdate(
                    EXECUTOR_TRANSACTION_EVENT_DONE,
                    NULL);
    ExecutorTransaction_FillUpdate(&update, tx);

    if (tx->state == EXECUTOR_TRANSACTION_STATE_SENT) {
        tx->state = EXECUTOR_TRANSACTION_STATE_ACKED;
    }

    if (tx->state != EXECUTOR_TRANSACTION_STATE_ACKED &&
            tx->state != EXECUTOR_TRANSACTION_STATE_DATA_SEEN) {
        update.event = EXECUTOR_TRANSACTION_EVENT_PROTOCOL_ERROR;
        update.reason = "DONE in invalid transaction state";
        return update;
    }

    if ((tx->response_policy == EXECUTOR_TRANSACTION_RESPONSE_DATA_THEN_DONE ||
            tx->response_policy == EXECUTOR_TRANSACTION_RESPONSE_MULTI_DATA_THEN_DONE) &&
            tx->data_count == 0U) {
        update.event = EXECUTOR_TRANSACTION_EVENT_PROTOCOL_ERROR;
        update.reason = "DONE before required DATA";
        return update;
    }

    tx->state = EXECUTOR_TRANSACTION_STATE_DONE;
    tx->active = false;
    return update;
}

bool ExecutorTransactionTable_CheckTimeouts(
        ExecutorTransactionTable_t* table,
        uint32_t now_ms,
        ExecutorTransactionUpdate_t* out_update)
{
    if (out_update != NULL) {
        *out_update = ExecutorTransaction_MakeUpdate(
                EXECUTOR_TRANSACTION_EVENT_NONE,
                NULL);
    }

    if (table == NULL) {
        return false;
    }

    for (uint8_t i = 0; i < JOB_MAX_EXECUTOR_TRANSACTIONS; i++) {
        ExecutorTransaction_t* tx = &table->entries[i];

        if (!tx->active) {
            continue;
        }

        if (tx->state == EXECUTOR_TRANSACTION_STATE_SENT &&
                ExecutorTransaction_TickElapsed(now_ms, tx->sent_time_ms, tx->ack_timeout_ms)) {
            tx->state = EXECUTOR_TRANSACTION_STATE_ACK_TIMEOUT;

            if (out_update != NULL) {
                ExecutorTransaction_FillUpdate(out_update, tx);
                out_update->event = EXECUTOR_TRANSACTION_EVENT_ACK_TIMEOUT;
                out_update->reason = "ACK timeout";
            }
            return true;
        }

        if ((tx->state == EXECUTOR_TRANSACTION_STATE_ACKED ||
                tx->state == EXECUTOR_TRANSACTION_STATE_DATA_SEEN) &&
                ExecutorTransaction_TickElapsed(
                        now_ms,
                        tx->sent_time_ms,
                        tx->operation_timeout_ms)) {
            tx->state = EXECUTOR_TRANSACTION_STATE_OPERATION_TIMEOUT;

            if (out_update != NULL) {
                ExecutorTransaction_FillUpdate(out_update, tx);
                out_update->event = EXECUTOR_TRANSACTION_EVENT_OPERATION_TIMEOUT;
                out_update->reason = "operation timeout";
            }
            return true;
        }
    }

    return false;
}
