/*
 * job_manager.c
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#include "Dispatcher/job_manager.h"
#include "Dispatcher/dispatcher_io.h"
#include "Dispatcher/can_packer.h"
#include "shared_resources.h"
#include "app_config.h"
#include <string.h>
#include <stdio.h>
#include "main.h" // Для HAL_GetTick()

// --- Внутренние переменные ---
static JobContext_t g_active_jobs[MAX_CONCURRENT_JOBS];
static uint32_t g_next_job_id = 1;

// --- Прототипы внутренних функций ---
static JobContext_t* JobManager_FindJob(uint32_t job_id);
static JobContext_t* JobManager_FindFreeSlot(void);
static void JobManager_ExecuteStep(JobContext_t* job);
static void JobManager_CompleteJob(JobContext_t* job, JobStatus_t final_status);
static void JobManager_SignalSystemReady(void);

// --- API функции ---

void JobManager_Init(void)
{
	for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
		g_active_jobs[i].status = JOB_STATUS_IDLE;
        g_active_jobs[i].job_id = 0;
     }
     g_next_job_id = 1;
}

uint32_t JobManager_StartNewJob(const UniversalCommand_t* parsed_cmd)
{
	JobContext_t* job = JobManager_FindFreeSlot();
    if (job == NULL) {
    	Dispatcher_SendUsbResponse("ERROR: No free job slots to start new job.");
        return 0;
    }

    job->job_id = g_next_job_id++;
    if (g_next_job_id == 0) g_next_job_id = 1;

    job->status = JOB_STATUS_RUNNING;
    job->initial_recipe_id = parsed_cmd->recipe_id;
    job->current_recipe = Recipe_Get(parsed_cmd->recipe_id);
    if (job->current_recipe == NULL) {
         char err_msg[APP_USB_RESP_MAX_LEN];
         snprintf(err_msg, sizeof(err_msg), "ERROR: Job %lu: Unknown recipe ID %d.", (unsigned long)job->job_id, parsed_cmd->recipe_id);
         Dispatcher_SendUsbResponse(err_msg);
         JobManager_CompleteJob(job, JOB_STATUS_ERROR);
         return 0;
    }
    job->current_step_index = 0;
    job->pending_actions_count = 0;
    job->step_start_time_ms = HAL_GetTick();
    job->initial_cmd = *parsed_cmd;

    char ack_msg[APP_USB_RESP_MAX_LEN];
    snprintf(ack_msg, sizeof(ack_msg), "INFO: Job #%lu started (Recipe ID:%d).", (unsigned long)job->job_id, job->initial_recipe_id);
    Dispatcher_SendUsbResponse(ack_msg);

    JobManager_ExecuteStep(job);
    return job->job_id;
}

bool JobManager_ProcessExecutorResponse(uint32_t job_id, uint8_t executor_id, bool action_status_ok)
{
	JobContext_t* job = JobManager_FindJob(job_id);
    if (job == NULL || job->status != JOB_STATUS_RUNNING) {
             char err_msg[APP_USB_RESP_MAX_LEN];
             snprintf(err_msg, sizeof(err_msg), "WARNING: Response for unknown/inactive Job #%lu from Exec %u.", (unsigned long)job_id, executor_id);
             Dispatcher_SendUsbResponse(err_msg);
             return false;
    }

     if (!action_status_ok) {
             char err_msg[APP_USB_RESP_MAX_LEN];
             snprintf(err_msg, sizeof(err_msg), "ERROR: Job #%lu: Exec %u reported error for step %u.", (unsigned long)job->job_id, executor_id, job->current_step_index);
             Dispatcher_SendUsbResponse(err_msg);
             JobManager_CompleteJob(job, JOB_STATUS_ERROR);
             return true;
     }

      if (job->pending_actions_count > 0) {
          job->pending_actions_count--;
      } else {
        	char warn_msg[APP_USB_RESP_MAX_LEN];
            snprintf(warn_msg, sizeof(warn_msg), "WARNING: Job #%lu: Duplicate/unexpected response for step %u from Exec %u.", (unsigned long)job->job_id, job->current_step_index, executor_id);
            Dispatcher_SendUsbResponse(warn_msg);
            return false;
      }

      if (job->pending_actions_count == 0) {
          job->current_step_index++;
          JobManager_ExecuteStep(job);
      }
      return true;
}

void JobManager_Run(void)
{
	for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
		JobContext_t* job = &g_active_jobs[i];
		if (job->status == JOB_STATUS_RUNNING) {
			if ((HAL_GetTick() - job->step_start_time_ms) > JOB_TIMEOUT_MS) {
				char err_msg[APP_USB_RESP_MAX_LEN];
				snprintf(err_msg, sizeof(err_msg), "ERROR: Job #%lu timed out at step %u.", (unsigned long)job->job_id, job->current_step_index);
				Dispatcher_SendUsbResponse(err_msg);
				JobManager_CompleteJob(job, JOB_STATUS_TIMEOUT);
				continue;
            }
            const ProcessStep_t* current_step = &job->current_recipe[job->current_step_index];
            if (current_step->num_actions == 1 && current_step->atomic_actions[0].action == ACTION_WAIT_MS) {
                if ((HAL_GetTick() - job->step_start_time_ms) >= current_step->atomic_actions[0].params.wait.delay_ms) {
                    JobManager_ProcessExecutorResponse(job->job_id, 0, true);
                }
            }
		}
	}
}

// --- Внутренние функции ---

static JobContext_t* JobManager_FindJob(uint32_t job_id)
{
	for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
		if (g_active_jobs[i].status != JOB_STATUS_IDLE && g_active_jobs[i].job_id == job_id) {
			return &g_active_jobs[i];
        }
    }
	return NULL;
}

static JobContext_t* JobManager_FindFreeSlot(void)
{
	for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
		if (g_active_jobs[i].status == JOB_STATUS_IDLE) {
			return &g_active_jobs[i];
        }
    }
	return NULL;
}

static void JobManager_ExecuteStep(JobContext_t* job)
{
	const ProcessStep_t* current_step = &job->current_recipe[job->current_step_index];
    if (current_step->atomic_actions == NULL || current_step->num_actions == 0) {
    	JobManager_CompleteJob(job, JOB_STATUS_COMPLETED);
    	return;
    }

    job->step_start_time_ms = HAL_GetTick();
    job->pending_actions_count = current_step->num_actions;

    char info_msg[APP_USB_RESP_MAX_LEN];
    snprintf(info_msg, sizeof(info_msg), "INFO: Job #%lu: Executing step %u (%u actions).", (unsigned long)job->job_id, job->current_step_index, current_step->num_actions);
    Dispatcher_SendUsbResponse(info_msg);

    for (int i = 0; i < current_step->num_actions; i++) {
	    const AtomicAction_t* action = &current_step->atomic_actions[i];
        
        bool should_execute = true;

        if (job->initial_recipe_id == RECIPE_INITIALIZE_SYSTEM && action->action == ACTION_HOME_MOTOR)
        {
            if (job->initial_cmd.args_type == ARGS_TYPE_BINARY && job->initial_cmd.args.binary.len > 0)
            {
                uint8_t modules_mask = job->initial_cmd.args.binary.raw[0];
                uint8_t motor_id_in_recipe = action->params.home_motor.motor_id;
                if (motor_id_in_recipe > 0)
                {
                    uint8_t motor_bit = 1 << (motor_id_in_recipe - 1);
                    if (!(modules_mask & motor_bit)) {
                        should_execute = false;
                    }
                }
            }
        }
        // --- [ADD_NEW_COMMAND] ---
        // 5. Если ваша новая команда параметризованная, добавьте ее логику фильтрации здесь
        // else if (job->initial_recipe_id == RECIPE_WASH_CUVETTE && action->action == ACTION_WASH)
        // {
        //     // ... ваша логика фильтрации ...
        // }
        // --- КОНЕЦ ФИЛЬТРУЮЩЕЙ ЛОГИКИ ---
        
        if (!should_execute) {
            job->pending_actions_count--;
            char filter_msg[APP_USB_RESP_MAX_LEN];
            snprintf(filter_msg, sizeof(filter_msg), "DEBUG: Job #%lu: Action for motor_id=%u filtered out by mask.", (unsigned long)job->job_id, action->params.home_motor.motor_id);
            Dispatcher_SendUsbResponse(filter_msg);
            continue;
        }

        CAN_Message_t can_msg;
        switch (action->action) {
            case ACTION_ROTATE_MOTOR:
                snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Sent ROTATE_MOTOR (ID:%u, Steps:%ld, Speed:%u) to Exec.",
                    (unsigned long)job->job_id, action->params.rotate_motor.motor_id, (long)action->params.rotate_motor.steps, action->params.rotate_motor.speed);
                Dispatcher_SendUsbResponse(info_msg);
                Packer_CreateRotateMotorMsg(action->params.rotate_motor.motor_id, action->params.rotate_motor.steps, action->params.rotate_motor.speed, job->job_id, &can_msg);
                xQueueSend(can_tx_queue_handle, &can_msg, 0);
                break;
            case ACTION_START_PUMP:
                snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Sent START_PUMP (ID:%u) to Exec.", (unsigned long)job->job_id, action->params.pump.pump_id);
                Dispatcher_SendUsbResponse(info_msg);
                Packer_CreateStartPumpMsg(action->params.pump.pump_id, job->job_id, &can_msg);
                xQueueSend(can_tx_queue_handle, &can_msg, 0);
                break;
            case ACTION_STOP_PUMP:
                snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Sent STOP_PUMP (ID:%u) to Exec.", (unsigned long)job->job_id, action->params.pump.pump_id);
                Dispatcher_SendUsbResponse(info_msg);
                Packer_CreateStopPumpMsg(action->params.pump.pump_id, job->job_id, &can_msg);
                xQueueSend(can_tx_queue_handle, &can_msg, 0);
                break;
            case ACTION_HOME_MOTOR:
                snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Sent HOME_MOTOR (ID:%u, Speed:%u) to Exec.",
                    (unsigned long)job->job_id, action->params.home_motor.motor_id, action->params.home_motor.speed);
                Dispatcher_SendUsbResponse(info_msg);
                Packer_CreateHomeMotorMsg(action->params.home_motor.motor_id, action->params.home_motor.speed, job->job_id, &can_msg);
                xQueueSend(can_tx_queue_handle, &can_msg, 0);
                break;
            case ACTION_WAIT_MS:
                snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Started WAIT_MS for %lu ms.", (unsigned long)job->job_id, (unsigned long)action->params.wait.delay_ms);
                Dispatcher_SendUsbResponse(info_msg);
                job->pending_actions_count--;
                break;
            default:
                snprintf(info_msg, sizeof(info_msg), "ERROR: Job #%lu: Unknown action %d in step %u.", (unsigned long)job->job_id, action->action, job->current_step_index);
                Dispatcher_SendUsbResponse(info_msg);
                JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                return;
        }
    }

    if (job->pending_actions_count == 0) {
        job->current_step_index++;
        JobManager_ExecuteStep(job);
    }
}

static void JobManager_CompleteJob(JobContext_t* job, JobStatus_t final_status)
{
	job->status = final_status;
    char final_msg[APP_USB_RESP_MAX_LEN];
    snprintf(final_msg, sizeof(final_msg), "INFO: Job #%lu finished with status %d.", (unsigned long)job->job_id, final_status);
    Dispatcher_SendUsbResponse(final_msg);

    if (job->initial_recipe_id == RECIPE_INITIALIZE_SYSTEM && final_status == JOB_STATUS_COMPLETED) {
         JobManager_SignalSystemReady();
    }

    job->status = JOB_STATUS_IDLE;
    job->job_id = 0;
}

static void JobManager_SignalSystemReady(void) {
    Dispatcher_SendUsbResponse("DEBUG: Signaling system READY.");
}