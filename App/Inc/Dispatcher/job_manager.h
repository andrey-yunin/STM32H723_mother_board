/*
 * job_manager.h
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_JOB_MANAGER_H_
#define INC_DISPATCHER_JOB_MANAGER_H_


#include <stdbool.h>
#include <stdint.h>
#include "Dispatcher/host_command_model.h"

// --- API модуля Job Manager ---

typedef struct {
	uint32_t job_id;
	uint16_t host_command_code;
	RecipeID_t recipe_id;
	uint16_t host_status_code;
	bool completed;
} JobManagerCompletionEvent_t;

typedef void (*JobManagerCompletionCallback_t)(
		const JobManagerCompletionEvent_t* event);

void JobManager_Init(void);

void JobManager_SetCompletionCallback(JobManagerCompletionCallback_t callback);

uint32_t JobManager_StartNewJob(const UniversalCommand_t* parsed_cmd);

void JobManager_AbortAll(uint16_t host_status_code);
bool JobManager_HasActiveJob(void);

void JobManager_Run(void);


#endif /* INC_DISPATCHER_JOB_MANAGER_H_ */
