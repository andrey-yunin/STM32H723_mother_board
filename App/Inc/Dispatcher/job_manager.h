/*
 * job_manager.h
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_JOB_MANAGER_H_
#define INC_DISPATCHER_JOB_MANAGER_H_


#include <stdint.h>
#include <stdbool.h>
#include "Dispatcher/command_parser.h"
#include "Dispatcher/recipe_store.h"

// --- Типы данных ---

typedef enum {
    JOB_STATUS_IDLE,
    JOB_STATUS_RUNNING,
    JOB_STATUS_PAUSED,
    JOB_STATUS_COMPLETED,
    JOB_STATUS_TIMEOUT,
    JOB_STATUS_ERROR
} JobStatus_t;


typedef enum {
	JOB_KIND_HOST_RECIPE = 0,
    JOB_KIND_HOST_DIRECT
} JobKind_t;


typedef enum {
	JOB_RESPONSE_DONE_ONLY = 0,
    JOB_RESPONSE_DATA_THEN_DONE,
    JOB_RESPONSE_MULTI_DATA_THEN_DONE
} JobResponsePolicy_t;


typedef enum {
	EXEC_TX_IDLE = 0,
    EXEC_TX_SENT,
    EXEC_TX_ACKED,
    EXEC_TX_DATA_SEEN,
    EXEC_TX_DONE,
    EXEC_TX_NACK,
    EXEC_TX_ACK_TIMEOUT,
    EXEC_TX_OPERATION_TIMEOUT
} ExecutorTransactionState_t;

typedef struct {
	bool active;
    uint32_t job_id;

    // Host command, на которую Дирижер формирует Host DATA/DONE.
    uint16_t host_command_code;

    // Low-level command, по которой исполнитель отвечает ACK/DATA/DONE/NACK.
    uint16_t low_command_code;

    uint8_t expected_node_id;
    uint8_t expected_channel;
    bool channel_valid;

    JobResponsePolicy_t response_policy;
    ExecutorTransactionState_t state;

    uint32_t sent_time_ms;
    uint32_t ack_timeout_ms;
    uint32_t operation_timeout_ms;

    uint16_t last_error_code;
    uint8_t data_count;
} ExecutorTransaction_t;

typedef enum {
	JOB_INTERNAL_ACTION_WAIT_MS = 0
} JobInternalActionType_t;

typedef struct {
     bool active;
     JobInternalActionType_t type;
     uint32_t started_at_ms;
     uint32_t duration_ms;
 } JobInternalAction_t;

typedef struct {
    uint32_t job_id;
    JobStatus_t status;
    JobKind_t kind;
    RecipeID_t initial_recipe_id;
    const ProcessStep_t* current_recipe;
    uint8_t current_step_index;
    uint8_t pending_actions_count;
    uint32_t step_start_time_ms;
    uint32_t step_timeout_ms;
    ExecutorTransaction_t transactions[JOB_MAX_EXECUTOR_TRANSACTIONS];
    UniversalCommand_t initial_cmd;
    JobInternalAction_t internal_actions[JOB_MAX_INTERNAL_ACTIONS];
} JobContext_t;




// --- API модуля Job Manager ---

void JobManager_Init(void);

uint32_t JobManager_StartNewJob(const UniversalCommand_t* parsed_cmd);

bool JobManager_ProcessExecutorResponse(uint32_t job_id, uint8_t executor_id, bool action_status_ok);

void JobManager_Run(void);


#endif /* INC_DISPATCHER_JOB_MANAGER_H_ */
