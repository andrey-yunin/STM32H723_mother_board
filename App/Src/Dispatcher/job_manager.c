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
#include "app_init_checker.h"
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

    // Сохраняем ID до того, как он может быть обнулен в JobManager_CompleteJob
    const uint32_t new_job_id = g_next_job_id++;
    if (g_next_job_id == 0) g_next_job_id = 1;

    job->job_id = new_job_id;
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
    
    // Возвращаем сохраненный ID, так как job->job_id может быть уже равен 0
    return new_job_id;
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

// --- Helper functions for dynamic parameter retrieval ---
static uint8_t JobManager_GetUint8Param(const JobContext_t* job, ParamSource_t source, uint8_t static_value)
{
	if (job->initial_cmd.args_type == ARGS_TYPE_PARSED) {
		switch (source) {

			case PARAM_SOURCE_CMD_INIT_MASK:
				// This source is designed for the overall mask (e.g., in INIT command),
				// not usually for a single motor_id value.
				// However, if a parameter is *intended* to be the modules_mask value itself,
				// it would be returned here. For specific motor_id, this case is less likely
				// but included for completeness.
				return job->initial_cmd.args.init.modules_mask;

			case PARAM_SOURCE_CMD_DISPENSER_ID:
				return job->initial_cmd.args.dispenser_wash.dispenser_id;

			 case PARAM_SOURCE_CMD_WASH_STATION_WASH_CYCLES: // <-- added 05/02/2026
				 return job->initial_cmd.args.wash_station_wash.cycles;



				// Add other uint8_t sources here as needed for other commands


				default:
					break; // Fall through to static_value if source not found or is static
					}
		}
	return static_value; // Return static value if not parsed or source is static
}

static uint16_t JobManager_GetUint16Param(const JobContext_t* job, ParamSource_t source, uint16_t static_value)
{
	if (job->initial_cmd.args_type == ARGS_TYPE_PARSED) {
		switch (source) {

			case PARAM_SOURCE_CMD_DISPENSER_CYCLES:
				return job->initial_cmd.args.dispenser_wash.cycles;

	//		case PARAM_SOURCE_CMD_WASH_STATION_WASH_CUVETTE: // <-- added 05/02/2026
		//		return job->initial_cmd.args.wash_station_wash.cuvette;

			default:
				break;
				}
		}
	return static_value;
}

static int32_t JobManager_GetInt32Param(const JobContext_t* job, ParamSource_t source, int32_t static_value)
{
	if (job->initial_cmd.args_type == ARGS_TYPE_PARSED) {
		switch (source) {
		// Add int32_t sources here as needed
		case PARAM_SOURCE_CMD_WASH_STATION_WASH_ROTATE_STEPS: // added 06.02.2026 Changed from CUVETTE to ROTATE_STEPS
			return (int32_t)job->initial_cmd.args.wash_station_wash.rotate_steps; // Direct access to int32_t rotate_steps

		case PARAM_SOURCE_CMD_SAMPLE_ROTATE_STEPS: // added 11/02/2026
			return job->initial_cmd.args.sample_rotate.rotate_steps;
		default:
			break;
			}
		}
	return static_value;
}

static uint32_t JobManager_GetUint32Param(const JobContext_t* job, ParamSource_t source, uint32_t static_value)
{
	if (job->initial_cmd.args_type == ARGS_TYPE_PARSED) {
		switch (source) {
			case PARAM_SOURCE_CMD_DISPENSER_VOLUME:
				return job->initial_cmd.args.dispenser_wash.volume;

				// Add other uint32_t sources here as needed
				// For example:
				// case PARAM_SOURCE_CMD_WAIT_DELAY: return job->initial_cmd.args.wait.delay_ms;
				default:
					break; // Fall through to static_value
					}
		}
	return static_value;
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

            // added 04/02/2026--- NEW: Determine effective delay_ms for ACTION_WAIT_MS ---
            // We need to get the effective action here because current_step->atomic_actions[0] is const
            uint32_t effective_wait_delay_ms = JobManager_GetUint32Param(
            		job, current_step->atomic_actions[0].params.wait.delay_ms_source,current_step->atomic_actions[0].params.wait.delay_ms);
            if ((HAL_GetTick() - job->step_start_time_ms) >= effective_wait_delay_ms) { // Use dynamic delay_ms
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

        // Determine effective parameters for ACTION_HOME_MOTOR
        // These will be used for both filtering and dispatching the command
        uint8_t effective_home_motor_id = JobManager_GetUint8Param(job, action->params.home_motor.motor_id_source, action->params.home_motor.motor_id);
        uint16_t effective_home_motor_speed = JobManager_GetUint16Param(job, action->params.home_motor.speed_source, action->params.home_motor.speed);

        if (job->initial_recipe_id == RECIPE_INITIALIZE_SYSTEM && action->action == ACTION_HOME_MOTOR)
        	{
        	if (job->initial_cmd.args_type == ARGS_TYPE_PARSED)
        		{
        		uint8_t modules_mask = job->initial_cmd.args.init.modules_mask;
        		// Use the effective_home_motor_id for filtering
        		if (effective_home_motor_id > 0)
        			{
        			uint8_t motor_bit = 1 << (effective_home_motor_id - 1);
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
            	// added 04/02/2026 --- NEW: Determine effective parameters for ACTION_ROTATE_MOTOR ---
            	uint8_t effective_rotate_motor_id = JobManager_GetUint8Param(job, action->params.rotate_motor.motor_id_source,
            	      action->params.rotate_motor.motor_id);
            	int32_t effective_rotate_motor_steps = JobManager_GetInt32Param(job, action->params.rotate_motor.steps_source,
            	      action->params.rotate_motor.steps);
            	uint16_t effective_rotate_motor_speed = JobManager_GetUint16Param(job, action->params.rotate_motor.speed_source,
            	      action->params.rotate_motor.speed);
            	// --- END NEW ---

            	snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Sent ROTATE_MOTOR (ID:%u, Steps:%ld, Speed:%u) to Exec.",
            			(unsigned long)job->job_id, effective_rotate_motor_id, (long)effective_rotate_motor_steps, effective_rotate_motor_speed);
            	Dispatcher_SendUsbResponse(info_msg);
            	Packer_CreateRotateMotorMsg(effective_rotate_motor_id, effective_rotate_motor_steps, effective_rotate_motor_speed, job->job_id, &can_msg);
            	xQueueSend(can_tx_queue_handle, &can_msg, 0);
            	job->pending_actions_count--; // Simulate response
            	break;

            case ACTION_START_PUMP:
            	// added 04/02/2026--- NEW: Determine effective parameters for ACTION_START_PUMP ---
            	uint8_t effective_start_pump_id = JobManager_GetUint8Param(job, action->params.pump.pump_id_source, action->params.pump.pump_id);
            	// --- END NEW ---
            	snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Sent START_PUMP (ID:%u) to Exec.", (unsigned long)job->job_id,
            			effective_start_pump_id);
            	Dispatcher_SendUsbResponse(info_msg);
            	Packer_CreateStartPumpMsg(effective_start_pump_id, job->job_id, &can_msg);
            	xQueueSend(can_tx_queue_handle, &can_msg, 0);
            	job->pending_actions_count--; // Simulate response
            	break;


            case ACTION_STOP_PUMP:
            	// added 04/02/2026--- NEW: Determine effective parameters for ACTION_STOP_PUMP ---
            	uint8_t effective_stop_pump_id = JobManager_GetUint8Param(job, action->params.pump.pump_id_source, action->params.pump.pump_id);
            	// --- END NEW ---
            	snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Sent STOP_PUMP (ID:%u) to Exec.", (unsigned long)job->job_id,
            			effective_stop_pump_id);
            	Dispatcher_SendUsbResponse(info_msg);
            	Packer_CreateStopPumpMsg(effective_stop_pump_id, job->job_id, &can_msg);
            	xQueueSend(can_tx_queue_handle, &can_msg, 0);
            	job->pending_actions_count--; // Simulate response
            	break;


            case ACTION_HOME_MOTOR:
                snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Sent HOME_MOTOR (ID:%u, Speed:%u) to Exec.",
                    (unsigned long)job->job_id, effective_home_motor_id, effective_home_motor_speed); // added 04/02/2026 ТЕПЕРЬ ИСПОЛЬЗУЕМ effective_home_motor_id/speed
                Dispatcher_SendUsbResponse(info_msg);
                Packer_CreateHomeMotorMsg(effective_home_motor_id, effective_home_motor_speed, job->job_id, &can_msg); // added 04/02/2026 ТЕПЕРЬ ИСПОЛЬЗУЕМ effective_home_motor_id/speed
                xQueueSend(can_tx_queue_handle, &can_msg, 0);
                job->pending_actions_count--; // Simulate response
                break;

            case ACTION_WAIT_MS:
            	// added 04/02/2026--- NEW: Determine effective parameters for ACTION_WAIT_MS ---
            	uint32_t effective_wait_delay_ms = JobManager_GetUint32Param(job, action->params.wait.delay_ms_source, action->params.wait.delay_ms);
            	// --- END NEW ---

            	snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Started WAIT_MS for %lu ms.", (unsigned long)job->job_id,
            			(unsigned long)effective_wait_delay_ms);
            	Dispatcher_SendUsbResponse(info_msg);
            	// Note: The actual waiting logic often happens outside this switch, possibly in JobManager_Run()
            	// For now, we update the log message to reflect the dynamic parameter.
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

    // Отправляем бинарный DONE-ответ
    // 0x0000 - успешное завершение, другие коды - для ошибок/статусов
    uint16_t done_status_code = (final_status == JOB_STATUS_COMPLETED) ? 0x0000 : 0x0001;
    Dispatcher_SendDone(job->initial_cmd.command_code, done_status_code);


    if (job->initial_recipe_id == RECIPE_INITIALIZE_SYSTEM && final_status == JOB_STATUS_COMPLETED) {
         JobManager_SignalSystemReady();
    }

    job->status = JOB_STATUS_IDLE;
    job->job_id = 0;
}

static void JobManager_SignalSystemReady(void) {
    Dispatcher_SendUsbResponse("DEBUG: Signaling system READY.");
	SetSystemReady();
}
