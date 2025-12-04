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

// --- Константы ---
#define MAX_CONCURRENT_JOBS     5
#define JOB_TIMEOUT_MS          5000

// --- Типы данных ---

typedef enum {
    JOB_STATUS_IDLE,
    JOB_STATUS_RUNNING,
    JOB_STATUS_PAUSED,
    JOB_STATUS_COMPLETED,
    JOB_STATUS_TIMEOUT,
    JOB_STATUS_ERROR
} JobStatus_t;

typedef struct {
    uint32_t job_id;
    JobStatus_t status;
    RecipeID_t initial_recipe_id;
    const ProcessStep_t* current_recipe;
    uint8_t current_step_index;
    uint8_t pending_actions_count;
    uint32_t step_start_time_ms;
    ParsedCommand_t initial_cmd;
} JobContext_t;

// --- API модуля Job Manager ---

void JobManager_Init(void);

void JobManager_Init(void);

uint32_t JobManager_StartNewJob(const ParsedCommand_t* parsed_cmd);

bool JobManager_ProcessExecutorResponse(uint32_t job_id, uint8_t executor_id, bool action_status_ok);

void JobManager_Run(void);


#endif /* INC_DISPATCHER_JOB_MANAGER_H_ */

