/*
 * job_manager.c
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#include "Dispatcher/job_manager.h"
#include "Dispatcher/param_translator.h"
#include "Dispatcher/calibrator.h"
#include "Dispatcher/dispatcher_io.h"
#include "Dispatcher/can_packer.h"
#include "Dispatcher/device_mapping.h"
#include "Dispatcher/system_mapping.h"
#include "Dispatcher/service_manager.h"
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

static uint32_t JobManager_AbsSteps(int32_t steps)
{
	// INT32_MIN нельзя безопасно инвертировать простым "-steps".
    return (steps < 0) ? ((uint32_t)(-(steps + 1)) + 1) : (uint32_t)steps;
}

static void JobManager_ExtendStepTimeout(JobContext_t* job, uint32_t required_timeout_ms)
{
	if (required_timeout_ms > job->step_timeout_ms) {
		job->step_timeout_ms = required_timeout_ms;
		}
}

static uint32_t JobManager_CalcMotionRotateTimeoutMs(int32_t steps, uint16_t speed)
{
	uint32_t abs_steps = JobManager_AbsSteps(steps);

	if (abs_steps == 0) {
		return JOB_TIMEOUT_MS;
	}

	// speed уже проверен вызывающим кодом: здесь считаем только физическое время.
	uint64_t motion_ms = (((uint64_t)abs_steps * 1000) + speed - 1) / speed;

	if (motion_ms > (UINT32_MAX - JOB_MOTION_ROTATE_MARGIN_MS)) {
		return UINT32_MAX;
	}
	return (uint32_t)motion_ms + JOB_MOTION_ROTATE_MARGIN_MS;
}



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
	if (parsed_cmd->command_code == 0x1002) { // Если это INIT
		uint8_t mask = (parsed_cmd->args_type == ARGS_TYPE_PARSED) ? parsed_cmd->args.init.modules_mask : 0xFF;
		if (!ServiceManager_CheckInventory(mask)) {
			Dispatcher_SendError(parsed_cmd->command_code, 0x0005);
			Dispatcher_SendUsbResponse("ERROR: Required CAN nodes for this module are OFFLINE.");
			return 0; // Блокируем запуск задания
			}
	}


	JobContext_t* job = JobManager_FindFreeSlot();

    if (job == NULL) {
    	Dispatcher_SendUsbResponse("ERROR: No free job slots to start new job.");
        Dispatcher_SendError(parsed_cmd->command_code, 0x0004); // ERR_BUSY / SYSTEM_BUSY
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
         snprintf(err_msg, sizeof(err_msg), "ERROR: Job %lu: Unknown recipe ID %d.", (unsigned long)job->job_id, (int)parsed_cmd->recipe_id);
         Dispatcher_SendUsbResponse(err_msg);
         JobManager_CompleteJob(job, JOB_STATUS_ERROR);
         return 0;
    }
    job->current_step_index = 0;
    job->pending_actions_count = 0;
    job->step_start_time_ms = HAL_GetTick();
    job->step_timeout_ms = JOB_TIMEOUT_MS;
    job->initial_cmd = *parsed_cmd;

    char ack_msg[APP_USB_RESP_MAX_LEN];
    snprintf(ack_msg, sizeof(ack_msg), "INFO: Job #%lu started (Recipe ID:%d).", (unsigned long)job->job_id, (int)job->initial_recipe_id);
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

			case PARAM_SOURCE_DISPENSER_ID:
				switch (job->initial_recipe_id) {
					case RECIPE_DISPENSER_WASH:
						return job->initial_cmd.args.dispenser_wash.dispenser_id;
					case RECIPE_DISPENSER_ASPIRATE:
						return job->initial_cmd.args.dispenser_aspirate.dispenser_id;
					case RECIPE_DISPENSER_DISPENSE:
						return job->initial_cmd.args.dispenser_dispense.dispenser_id;
					default:
						break;
					}
				break;

			case PARAM_SOURCE_WASH_STATION_CYCLES:
				return job->initial_cmd.args.wash_station_wash.cycles;

			 case PARAM_SOURCE_MIXER_ID:
				 return job->initial_cmd.args.mixer_mix.mixer_id;

			 case PARAM_SOURCE_PHOTOMETER_WAVELENGTH_MASK:
				 return job->initial_cmd.args.photometer_scan_single.wavelength_mask;



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

			case PARAM_SOURCE_DISPENSER_CYCLES:
				return job->initial_cmd.args.dispenser_wash.cycles;

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
		case PARAM_SOURCE_REACTION_DISK_ROTATE_STEPS:
			switch (job->initial_recipe_id) {
				case RECIPE_WASH_STATION_WASH:
					return ParamTranslator_CuvetteToSteps(job->initial_cmd.args.wash_station_wash.cuvette);
				case RECIPE_WASH_STATION_FILL:
					return ParamTranslator_CuvetteToSteps(job->initial_cmd.args.wash_station_fill.cuvette);
				case RECIPE_PHOTOMETER_SCAN_SINGLE:
					return ParamTranslator_CuvetteToSteps(job->initial_cmd.args.photometer_scan_single.cuvette);
				default:
					break;
				}
			break;

		case PARAM_SOURCE_REAGENT_SAMPLE_ROTATE_STEPS:
			switch (job->initial_recipe_id) {
				case RECIPE_SAMPLE_ROTATE:
					return ParamTranslator_SampleDiskSlotToSteps(job->initial_cmd.args.sample_rotate.slot);
				case RECIPE_REAGENT_ROTATE:
					return ParamTranslator_ReagentRotorSlotToSteps(
							job->initial_cmd.args.reagent_rotate.rotor_id,
							job->initial_cmd.args.reagent_rotate.slot);
				default:
					break;
				}
			break;

		case PARAM_SOURCE_DISPENSER_ROTATE_STEPS:
			switch (job->initial_recipe_id) {
				case RECIPE_DISPENSER_WASH:
					return job->initial_cmd.args.dispenser_wash.rotate_steps;
				case RECIPE_DISPENSER_ASPIRATE:
					return job->initial_cmd.args.dispenser_aspirate.rotate_steps;
				case RECIPE_DISPENSER_DISPENSE:
					return job->initial_cmd.args.dispenser_dispense.rotate_steps;
				default:
					break;
				}
			break;

		case PARAM_SOURCE_DISPENSER_Z_STEPS_DOWN:
			switch (job->initial_recipe_id) {
				case RECIPE_DISPENSER_WASH:
					return job->initial_cmd.args.dispenser_wash.steps_down;
				case RECIPE_DISPENSER_ASPIRATE:
					return job->initial_cmd.args.dispenser_aspirate.steps_down;
				case RECIPE_DISPENSER_DISPENSE:
					return job->initial_cmd.args.dispenser_dispense.steps_down;
				default:
					break;
				}
			break;

		case PARAM_SOURCE_DISPENSER_Z_STEPS_UP:
			switch (job->initial_recipe_id) {
				case RECIPE_DISPENSER_WASH:
					return job->initial_cmd.args.dispenser_wash.steps_up;
				case RECIPE_DISPENSER_ASPIRATE:
					return job->initial_cmd.args.dispenser_aspirate.steps_up;
				case RECIPE_DISPENSER_DISPENSE:
					return job->initial_cmd.args.dispenser_dispense.steps_up;
				default:
					break;
				}
			break;

		case PARAM_SOURCE_MIXER_XY_STEPS:
			return ParamTranslator_MixerCuvetteToXYSteps(job->initial_cmd.args.mixer_mix.cuvette);

		case PARAM_SOURCE_MIXER_Z_STEPS_DOWN:
			return ParamTranslator_MixerZToStepsDown(
					job->initial_cmd.args.mixer_mix.mixer_id,
					job->initial_cmd.args.mixer_mix.cuvette);

		case PARAM_SOURCE_MIXER_Z_STEPS_UP:
			return ParamTranslator_MixerZToStepsUp(
					job->initial_cmd.args.mixer_mix.mixer_id,
					job->initial_cmd.args.mixer_mix.cuvette);

		case PARAM_SOURCE_DISPENSER_SYRINGE_STEPS:
			switch (job->initial_recipe_id) {
				case RECIPE_DISPENSER_WASH:
					return job->initial_cmd.args.dispenser_wash.syringe_steps;
				case RECIPE_DISPENSER_ASPIRATE:
					return job->initial_cmd.args.dispenser_aspirate.syringe_steps;
				case RECIPE_DISPENSER_DISPENSE:
					return job->initial_cmd.args.dispenser_dispense.syringe_steps;
				default:
					break;
				}
			break;

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
			// case PARAM_SOURCE_CMD_DISPENSER_VOLUME: // This old source is removed
				// return job->initial_cmd.args.dispenser_wash.volume;
		case PARAM_SOURCE_WASH_STATION_FILL_DURATION_MS: {
			uint32_t duration_ms = 0;
			if (!Calibrator_PumpVolumeToDurationMs(SYS_WASH_PUMP_FILL,
					job->initial_cmd.args.wash_station_fill.volume_ul,
					CAL_PUMP_OP_FILL,
					&duration_ms)) {
				return 0;
				}
			return duration_ms;
			}

		case PARAM_SOURCE_MIXER_PADDLE_DURATION_MS:
			return job->initial_cmd.args.mixer_mix.duration_ms;

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
	// --- 1. Обработка входящих CAN-ответов от исполнителей (или имитатора) ---
	CAN_Message_t rx_msg;
	while (xQueueReceive(can_rx_queue_handle, &rx_msg, 0) == pdPASS) {
		CAN_Response_t response;
		if (!Packer_ParseCanResponse(&rx_msg, &response)) {
			Dispatcher_SendUsbResponse("WARNING: Invalid CAN RX frame, skipping.");
			continue;
			}

		switch (response.msg_type) {
			case CAN_MSG_TYPE_ACK:
				// ACK = подтверждение приёма, не продвигаем задание
				break;

			case CAN_MSG_TYPE_NACK: {
				// NACK = ошибка исполнителя → завершаем задание с ошибкой
				for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
					if (g_active_jobs[i].status == JOB_STATUS_RUNNING) {
						char err_msg[APP_USB_RESP_MAX_LEN];
						snprintf(err_msg, sizeof(err_msg),
								"ERROR: NACK from executor 0x%02X, cmd=0x%04X, err=0x%04X.",
								response.source_addr, (unsigned int)response.command_code, (unsigned int)response.error_code);
						Dispatcher_SendUsbResponse(err_msg);
						JobManager_ProcessExecutorResponse(
								g_active_jobs[i].job_id,
								response.source_addr,
								false);
						break;
						}
					}
				break;
				}

			case CAN_MSG_TYPE_DATA_DONE_LOG:
				switch (response.sub_type) {

					case CAN_SUB_TYPE_DONE:

						if (response.command_code == CAN_CMD_SRV_GET_INFO) {
							ServiceManager_UpdateNode(&response);
							}

						// DONE = исполнитель завершил действие → продвигаем задание
						for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
							if (g_active_jobs[i].status == JOB_STATUS_RUNNING) {
								JobManager_ProcessExecutorResponse(
										g_active_jobs[i].job_id,
										response.source_addr,
										true);
								break;
								}
							}
						break;

					case CAN_SUB_TYPE_DATA: {
						// DATA = Активная трансляция форматов для Хоста (LE -> BE)
						uint8_t host_data[16];
						uint16_t host_len = 0;


						// Внутренняя логика трансляции в зависимости от команды
						if (response.command_code == 0x9011 || response.command_code == 0x8000) {

							// Температура: [ID(1)][Temp_H(1)][Temp_L(1)][Status(1)]
							host_data[0] = response.ch_idx;
							host_data[1] = response.payload.raw[1]; // Temp MSB (от Исполнителя пришел LSB первым)
							host_data[2] = response.payload.raw[0]; // Temp LSB
							host_data[3] = (response.data_len >= 3) ? response.payload.raw[2] : 0x00;
							host_len = 4;
							}

						else if (response.command_code == 0x1000) {
							// Статус: [Status(1)][Err_H(1)][Err_L(1)]
							host_data[0] = response.payload.raw[0];
							host_data[1] = response.payload.raw[2]; // Error MSB
							host_data[2] = response.payload.raw[1]; // Error LSB
							host_len = 3;
							}



						else {
							// Универсальный проброс с разворотом байт для числовых данных
							host_data[0] = response.ch_idx;
							for (int j = 0; j < response.data_len && j < 15; j++) {
								host_data[j+1] = response.payload.raw[response.data_len - 1 - j];
								}

							host_len = response.data_len + 1;
							}

						// Отправка Хосту в бинарном формате CM> (Тип 0x03)
						Dispatcher_SendData(response.command_code, 0x03, 0x0000, host_data, host_len);
						break;
						}

					case CAN_SUB_TYPE_LOG: {

						// LOG = проброс текста в дебаг-консоль Дирижера
						char log_msg[APP_USB_RESP_MAX_LEN];
						snprintf(log_msg, sizeof(log_msg), "DEBUG: [0x%02X]: %s",
								response.source_addr, response.payload.log);
						Dispatcher_SendUsbResponse(log_msg);
						break;
						}

					default:
						// Неизвестный подтип — логируем для диагностики
						break;
						}
				break;

				default:
					break;
			}
		}

	// --- 2. Проверка таймаутов и обработка WAIT_MS (существующая логика) ---
	for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
		JobContext_t* job = &g_active_jobs[i];
		if (job->status == JOB_STATUS_RUNNING) {
			if ((HAL_GetTick() - job->step_start_time_ms) > job->step_timeout_ms) {
				char err_msg[APP_USB_RESP_MAX_LEN];
				snprintf(err_msg, sizeof(err_msg), "ERROR: Job #%lu timed out at step %u.", (unsigned long)job->job_id, (unsigned int)job->current_step_index);
						Dispatcher_SendUsbResponse(err_msg);
						JobManager_CompleteJob(job, JOB_STATUS_TIMEOUT);
						continue;
						}

			const ProcessStep_t* current_step = &job->current_recipe[job->current_step_index];
			if (current_step->num_actions == 1 && current_step->atomic_actions[0].action == ACTION_WAIT_MS) {
				uint32_t effective_wait_delay_ms = JobManager_GetUint32Param(
						job, current_step->atomic_actions[0].params.wait.delay_ms_source,
						current_step->atomic_actions[0].params.wait.delay_ms);
				if ((HAL_GetTick() - job->step_start_time_ms) >= effective_wait_delay_ms) {
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
    job->step_timeout_ms = JOB_TIMEOUT_MS;
    job->pending_actions_count = current_step->num_actions;

    char info_msg[APP_USB_RESP_MAX_LEN];
    snprintf(info_msg, sizeof(info_msg), "INFO: Job #%lu: Executing step %u (%u actions).", 
            (unsigned long)job->job_id, (unsigned int)job->current_step_index, (unsigned int)current_step->num_actions);
    Dispatcher_SendUsbResponse(info_msg);

    for (int i = 0; i < current_step->num_actions; i++) {
	    const AtomicAction_t* action = &current_step->atomic_actions[i];
        CAN_Message_t can_msg;
        DevicePhysAddr_t phys_addr;

        switch (action->action) {
            case ACTION_ROTATE_MOTOR: {
            	uint8_t sys_id = JobManager_GetUint8Param(job, action->params.rotate_motor.motor_id_source, action->params.rotate_motor.motor_id);
                int32_t steps = JobManager_GetInt32Param(job, action->params.rotate_motor.steps_source, action->params.rotate_motor.steps);
            	uint16_t speed = JobManager_GetUint16Param(job, action->params.rotate_motor.speed_source, action->params.rotate_motor.speed);
            	
            	uint32_t abs_steps = JobManager_AbsSteps(steps);
            	if (abs_steps != 0 && speed == 0) {
            		snprintf(info_msg, sizeof(info_msg),
            				"ERROR: Job #%lu: Invalid Motion speed 0 for SysID %u",
							(unsigned long)job->job_id, sys_id);

            		Dispatcher_SendUsbResponse(info_msg);
            		JobManager_CompleteJob(job, JOB_STATUS_ERROR);
            		return;
            		}

            	JobManager_ExtendStepTimeout(job, JobManager_CalcMotionRotateTimeoutMs(steps, speed));

            	phys_addr = DeviceMapping_GetMotorPhysAddr(sys_id);

            	if (!phys_addr.is_valid) {
            		snprintf(info_msg, sizeof(info_msg), "ERROR: Job #%lu: Invalid Motor SysID %u", (unsigned long)job->job_id, sys_id);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }

            	snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Sent ROTATE_MOTOR (Phys:%u:%u, Steps:%ld, Speed:%u)",
            			(unsigned long)job->job_id, phys_addr.node_id, phys_addr.ch_idx, (long)steps, speed);
            	Dispatcher_SendUsbResponse(info_msg);
            	
                Packer_CreateRotateMotorMsg(phys_addr.ch_idx, steps, speed, &can_msg);
                can_msg.id = CAN_BUILD_ID(CAN_PRIORITY_HIGH, CAN_MSG_TYPE_COMMAND, phys_addr.node_id, CAN_ADDR_CONDUCTOR);
            	xQueueSend(can_tx_queue_handle, &can_msg, 0);
            	break;
            }

            case ACTION_HOME_MOTOR: {
                uint8_t sys_id = JobManager_GetUint8Param(job, action->params.home_motor.motor_id_source, action->params.home_motor.motor_id);
                uint16_t speed = JobManager_GetUint16Param(job, action->params.home_motor.speed_source, action->params.home_motor.speed);


                // Фильтрация по маске для инициализации
                if (job->initial_recipe_id == RECIPE_INITIALIZE_SYSTEM) {
                    if (job->initial_cmd.args_type == ARGS_TYPE_PARSED) {
                        uint8_t modules_mask = job->initial_cmd.args.init.modules_mask;
                        if (!(modules_mask & (1 << (sys_id - 1)))) {
                            job->pending_actions_count--;
                            continue;
                        }
                    }
                }

                if (speed == 0) {
                	snprintf(info_msg, sizeof(info_msg),
                			"ERROR: Job #%lu: Invalid HOME speed 0 for SysID %u",
							(unsigned long)job->job_id, sys_id);

                	Dispatcher_SendUsbResponse(info_msg);
                	JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                	return;
                	}

                JobManager_ExtendStepTimeout(job, JOB_MOTION_HOME_TIMEOUT_MS);

                phys_addr = DeviceMapping_GetMotorPhysAddr(sys_id);
                if (!phys_addr.is_valid) {
                    snprintf(info_msg, sizeof(info_msg), "ERROR: Job #%lu: Invalid Motor SysID %u", (unsigned long)job->job_id, sys_id);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }

                snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Sent HOME_MOTOR (Phys:%u:%u, Speed:%u)",
                    (unsigned long)job->job_id, phys_addr.node_id, phys_addr.ch_idx, speed);
                Dispatcher_SendUsbResponse(info_msg);
                
                Packer_CreateHomeMotorMsg(phys_addr.ch_idx, speed, &can_msg);
                can_msg.id = CAN_BUILD_ID(CAN_PRIORITY_HIGH, CAN_MSG_TYPE_COMMAND, phys_addr.node_id, CAN_ADDR_CONDUCTOR);
                xQueueSend(can_tx_queue_handle, &can_msg, 0);
                break;
            }

            case ACTION_RUN_PUMP_DURATION: {
              	uint8_t sys_id = JobManager_GetUint8Param(job,
              			action->params.pump_duration.pump_id_source,
              			action->params.pump_duration.pump_id);

              	uint32_t duration_ms = JobManager_GetUint32Param(job,
              			action->params.pump_duration.duration_ms_source,
              			action->params.pump_duration.duration_ms);

              	if (duration_ms == 0) {
              		snprintf(info_msg, sizeof(info_msg),
              				"ERROR: Job #%lu: Invalid pump duration for SysID %u",
							(unsigned long)job->job_id, sys_id);
              		Dispatcher_SendUsbResponse(info_msg);
              		JobManager_CompleteJob(job, JOB_STATUS_ERROR);
              		return;
              	}

              	// Finite Fluidics: ждем физическую длительность операции плюс запас.
              	JobManager_ExtendStepTimeout(
              			job,
						duration_ms + JOB_PUMP_DURATION_MARGIN_MS);


              	phys_addr = DeviceMapping_GetFluidicPhysAddr(sys_id);
              	if (!phys_addr.is_valid) {
              		snprintf(info_msg, sizeof(info_msg),
              				"ERROR: Job #%lu: Invalid Pump SysID %u",
              				(unsigned long)job->job_id, sys_id);
              		Dispatcher_SendUsbResponse(info_msg);
              		JobManager_CompleteJob(job, JOB_STATUS_ERROR);
              		return;
              	}

              	snprintf(info_msg, sizeof(info_msg),
              			"DEBUG: Job #%lu: Sent RUN_PUMP_DURATION (Phys:%u:%u, Duration:%lu)",
              			(unsigned long)job->job_id,
              			phys_addr.node_id,
              			phys_addr.ch_idx,
              			(unsigned long)duration_ms);
              	Dispatcher_SendUsbResponse(info_msg);

              	Packer_CreatePumpRunDurationMsg(phys_addr.ch_idx, duration_ms, &can_msg);
              	can_msg.id = CAN_BUILD_ID(CAN_PRIORITY_HIGH, CAN_MSG_TYPE_COMMAND,
              			phys_addr.node_id, CAN_ADDR_CONDUCTOR);
              	xQueueSend(can_tx_queue_handle, &can_msg, 0);
              	break;
              }


            case ACTION_START_PUMP: {
            	uint8_t sys_id = JobManager_GetUint8Param(job, action->params.pump.pump_id_source, action->params.pump.pump_id);
                phys_addr = DeviceMapping_GetFluidicPhysAddr(sys_id);
                if (!phys_addr.is_valid) {
                    snprintf(info_msg, sizeof(info_msg), "ERROR: Job #%lu: Invalid Pump SysID %u", (unsigned long)job->job_id, sys_id);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }
            	snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Sent START_PUMP (Phys:%u:%u)", (unsigned long)job->job_id, phys_addr.node_id, phys_addr.ch_idx);
            	Dispatcher_SendUsbResponse(info_msg);
            	
                Packer_CreatePumpStartMsg(phys_addr.ch_idx, 0, &can_msg); // 0 = Default timeout
                can_msg.id = CAN_BUILD_ID(CAN_PRIORITY_HIGH, CAN_MSG_TYPE_COMMAND, phys_addr.node_id, CAN_ADDR_CONDUCTOR);
            	xQueueSend(can_tx_queue_handle, &can_msg, 0);
            	break;
            }

            case ACTION_STOP_PUMP: {
            	uint8_t sys_id = JobManager_GetUint8Param(job, action->params.pump.pump_id_source, action->params.pump.pump_id);
                phys_addr = DeviceMapping_GetFluidicPhysAddr(sys_id);
                if (!phys_addr.is_valid) {
                    snprintf(info_msg, sizeof(info_msg), "ERROR: Job #%lu: Invalid Pump SysID %u", (unsigned long)job->job_id, sys_id);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }
            	snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Sent STOP_PUMP (Phys:%u:%u)", (unsigned long)job->job_id, phys_addr.node_id, phys_addr.ch_idx);
            	Dispatcher_SendUsbResponse(info_msg);
            	
                Packer_CreatePumpStopMsg(phys_addr.ch_idx, &can_msg);
                can_msg.id = CAN_BUILD_ID(CAN_PRIORITY_HIGH, CAN_MSG_TYPE_COMMAND, phys_addr.node_id, CAN_ADDR_CONDUCTOR);
            	xQueueSend(can_tx_queue_handle, &can_msg, 0);
            	break;
            }

            case ACTION_WAIT_MS: {
            	uint32_t delay = JobManager_GetUint32Param(job, action->params.wait.delay_ms_source, action->params.wait.delay_ms);
            	snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Started WAIT_MS for %lu ms.", (unsigned long)job->job_id, (unsigned long)delay);
            	Dispatcher_SendUsbResponse(info_msg);
            	job->pending_actions_count--;
            	break;
            }

            case ACTION_PERFORM_SCAN: {
            	uint8_t sys_id = JobManager_GetUint8Param(job, action->params.perform_scan.photometer_id_source, action->params.perform_scan.photometer_id);
            	uint8_t mask = JobManager_GetUint8Param(job, action->params.perform_scan.wavelength_mask_source, action->params.perform_scan.wavelength_mask);
            	
                // Фотометр пока имеет фиксированный NodeID, добавим его позже в маппинг
            	snprintf(info_msg, sizeof(info_msg), "DEBUG: Job #%lu: Sent SCAN (SysID:%u, Mask:0x%02X)", (unsigned long)job->job_id, sys_id, mask);
            	Dispatcher_SendUsbResponse(info_msg);
            	
            	// Packer_CreatePerformScanMsg(sys_id, mask, &can_msg); 
                // Заглушка, пока не реализован упаковщик фотометра в новом стандарте
                job->pending_actions_count--; 
            	break;
            }

            default:
                snprintf(info_msg, sizeof(info_msg), "ERROR: Job #%lu: Unknown action %d", (unsigned long)job->job_id, (int)action->action);
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
