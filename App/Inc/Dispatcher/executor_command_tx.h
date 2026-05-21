#ifndef INC_DISPATCHER_EXECUTOR_COMMAND_TX_H_
#define INC_DISPATCHER_EXECUTOR_COMMAND_TX_H_

#include <stdint.h>
#include "Dispatcher/can_packer.h"
#include "Dispatcher/executor_transaction.h"

typedef enum {
    EXECUTOR_COMMAND_TX_OK = 0,
    EXECUTOR_COMMAND_TX_INVALID_ARG,
    EXECUTOR_COMMAND_TX_INVALID_FRAME,
    EXECUTOR_COMMAND_TX_DUPLICATE_TRANSACTION,
    EXECUTOR_COMMAND_TX_ROUTE_REGISTRATION_FAILED,
    EXECUTOR_COMMAND_TX_QUEUE_FULL,
    EXECUTOR_COMMAND_TX_QUEUE_SEND_FAILED
} ExecutorCommandTxStatus_t;

typedef struct {
    CAN_Message_t frame;
    ExecutorTransactionStart_t transaction;
} ExecutorCommandTxCommand_t;

ExecutorCommandTxStatus_t ExecutorCommandTx_SubmitBatch(
        ExecutorTransactionTable_t* transaction_table,
        const ExecutorCommandTxCommand_t* commands,
        uint8_t command_count);

const char* ExecutorCommandTx_StatusString(ExecutorCommandTxStatus_t status);

#endif /* INC_DISPATCHER_EXECUTOR_COMMAND_TX_H_ */
