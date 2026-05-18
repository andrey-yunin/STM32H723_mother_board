/*
 * job_manager.h
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_JOB_MANAGER_H_
#define INC_DISPATCHER_JOB_MANAGER_H_


#include <stdint.h>
#include "Dispatcher/command_parser.h"

// --- API модуля Job Manager ---

void JobManager_Init(void);

uint32_t JobManager_StartNewJob(const UniversalCommand_t* parsed_cmd);

void JobManager_Run(void);


#endif /* INC_DISPATCHER_JOB_MANAGER_H_ */
