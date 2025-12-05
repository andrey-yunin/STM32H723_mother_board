/*
 * job_manager.c
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */


#include "Dispatcher/job_manager.h"
#include "Dispatcher/dispatcher_io.h"
#include "Dispatcher/can_packer.h" // Будет определено позже
#include "shared_resources.h"
#include "app_config.h"
#include <string.h>
#include <stdio.h>
#include "main.h" // Для HAL_GetTick()

// --- Внутренние переменные ---
static JobContext_t g_active_jobs[MAX_CONCURRENT_JOBS]; // Пул активных заданий
static uint32_t g_next_job_id = 1;                     // Генератор уникальных ID для заданий

// --- Прототипы внутренних функций ---
static JobContext_t* JobManager_FindJob(uint32_t job_id);
static JobContext_t* JobManager_FindFreeSlot(void);
static void JobManager_ExecuteStep(JobContext_t* job);
static void JobManager_CompleteJob(JobContext_t* job, JobStatus_t final_status);
static void JobManager_SignalSystemReady(void); // Функция для сигнализации task_dispatcher

// --- API функции ---

/**
* @brief Инициализирует модуль Job Manager.
*        Должен быть вызван один раз при старте системы.
 */
void JobManager_Init(void)
{
	for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
		g_active_jobs[i].status = JOB_STATUS_IDLE;
        g_active_jobs[i].job_id = 0; // Сброс ID
     }
     g_next_job_id = 1; // Сброс ID генератора
}

/**
* @brief Запускает новое задание.
* @param parsed_cmd Разобранная команда.
* @return ID запущенного задания или 0 в случае ошибки (например, нет свободных слотов).
*/
uint32_t JobManager_StartNewJob(const ParsedCommand_t* parsed_cmd)
{
	JobContext_t* job = JobManager_FindFreeSlot();
    if (job == NULL) {
    	Dispatcher_SendUsbResponse("ERROR: No free job slots to start new job.");
        return 0;
        }

    job->job_id = g_next_job_id++; // Присваиваем уникальный ID
    if (g_next_job_id == 0) g_next_job_id = 1;   // Пропуск 0 и предотвращение переполнения
    job->status = JOB_STATUS_RUNNING;
    job->initial_recipe_id = parsed_cmd->recipe_id;
    job->current_recipe = Recipe_Get(parsed_cmd->recipe_id);
    if (job->current_recipe == NULL) {
         char err_msg[APP_USB_RESP_MAX_LEN];
         snprintf(err_msg, sizeof(err_msg), "ERROR: Job %lu: Unknown recipe ID %d.", job->job_id, parsed_cmd->recipe_id);
         Dispatcher_SendUsbResponse(err_msg);
         JobManager_CompleteJob(job, JOB_STATUS_ERROR);
    return 0;
    }
    job->current_step_index = 0;
    job->pending_actions_count = 0; // Будет установлено в JobManager_ExecuteStep
    job->step_start_time_ms = HAL_GetTick(); // Начало первого шага

    // Сохраняем оригинальную команду для контекста/логгирования
    job->initial_cmd = *parsed_cmd;

    char ack_msg[APP_USB_RESP_MAX_LEN];
    snprintf(ack_msg, sizeof(ack_msg), "INFO: Job #%lu started (Recipe ID:%d).", job->job_id, job->initial_recipe_id);
    Dispatcher_SendUsbResponse(ack_msg);

    // Запускаем выполнение первого шага задания
    JobManager_ExecuteStep(job);
    return job->job_id;
}


/**
* @brief Обрабатывает ответ от исполнителя и продвигает задание.
* @param job_id ID задания.
* @param executor_id ID исполнителя.
* @param action_status_ok Статус выполнения действия (true=успех, false=ошибка).
* @return true, если ответ успешно обработан, false в противном случае.
*/
bool JobManager_ProcessExecutorResponse(uint32_t job_id, uint8_t executor_id, bool action_status_ok)
{
	JobContext_t* job = JobManager_FindJob(job_id);
    if (job == NULL || job->status != JOB_STATUS_RUNNING) {
             // Ответ пришел для неактивного или несуществующего задания
             char err_msg[APP_USB_RESP_MAX_LEN];
             snprintf(err_msg, sizeof(err_msg), "WARNING: Response for unknown/inactive Job #%lu from Exec %u.", job_id, executor_id);
             Dispatcher_SendUsbResponse(err_msg);
             return false;
         }

     if (!action_status_ok) {
             char err_msg[APP_USB_RESP_MAX_LEN];
             snprintf(err_msg, sizeof(err_msg), "ERROR: Job #%lu: Exec %u reported error for step %u.", job->job_id, executor_id, job->current_step_index);
             Dispatcher_SendUsbResponse(err_msg);
             JobManager_CompleteJob(job, JOB_STATUS_ERROR);
             return true;
        }

        // Если pending_actions_count уже 0, возможно это дублированный ответ, игнорируем
      if (job->pending_actions_count > 0) {
          job->pending_actions_count--;
        } else {
        	char warn_msg[APP_USB_RESP_MAX_LEN];
            snprintf(warn_msg, sizeof(warn_msg), "WARNING: Job #%lu: Duplicate/unexpected response for step %u from Exec %u.", job->job_id, job->current_step_index, executor_id);
            Dispatcher_SendUsbResponse(warn_msg);
            return false;
        }


      if (job->pending_actions_count == 0) {
          // Все действия текущего шага завершены, переходим к следующему шагу
          job->current_step_index++;
          JobManager_ExecuteStep(job); // Запускаем следующий шаг или завершаем задание
        }
        return true;
    }

 /**
  * @brief Периодически вызывается для проверки таймаутов и выполнения внутренних шагов.
  */
void JobManager_Run(void)
{
	for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
    JobContext_t* job = &g_active_jobs[i];
    if (job->status == JOB_STATUS_RUNNING) { // Проверка таймаута для текущего шага
    	if ((HAL_GetTick() - job->step_start_time_ms) > JOB_TIMEOUT_MS) {
    		char err_msg[APP_USB_RESP_MAX_LEN];
            snprintf(err_msg, sizeof(err_msg), "ERROR: Job #%lu timed out at step %u.", job->job_id, job->current_step_index);
            Dispatcher_SendUsbResponse(err_msg);
            JobManager_CompleteJob(job, JOB_STATUS_TIMEOUT);
            continue; // Переходим к следующему заданию
            }
            // Обработка шагов, не требующих ответа от исполнителей (например, ACTION_WAIT_MS)
               const ProcessStep_t* current_step = &job->current_recipe[job->current_step_index];
            if (current_step->num_actions == 1 && current_step->atomic_actions[0].action == ACTION_WAIT_MS) {
                // Если это шаг ожидания, и время ожидания истекло
                if ((HAL_GetTick() - job->step_start_time_ms) >= current_step->atomic_actions[0].params.wait.delay_ms) {
                    char info_msg[APP_USB_RESP_MAX_LEN];
                    snprintf(info_msg, sizeof(info_msg), "INFO: Job #%lu: Wait step %u completed.", job->job_id, job->current_step_index);
                    Dispatcher_SendUsbResponse(info_msg);
                    job->pending_actions_count = 0; // Считаем, что "действие" ожидания завершено
                    // Имитируем завершение, чтобы перейти к следующему шагу.
                    // Передаем executor_id=0, action_status_ok=true, так как нет реального исполнителя.
               JobManager_ProcessExecutorResponse(job->job_id, 0, true);
                }
            }
       }
    }
}


// --- Внутренние функции ---

/**
* @brief Ищет задание по его ID.
* @param job_id ID задания.
* @return Указатель на JobContext_t или NULL, если задание не найдено.
*/
static JobContext_t* JobManager_FindJob(uint32_t job_id)
{
	for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
		if (g_active_jobs[i].status != JOB_STATUS_IDLE && g_active_jobs[i].job_id == job_id) {
        return &g_active_jobs[i];
        }
    }
return NULL;
}
/**
* @brief Ищет свободный слот для нового задания.
* @return Указатель на свободный JobContext_t или NULL, если нет свободных слотов.
*/
static JobContext_t* JobManager_FindFreeSlot(void)
{
	for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
		if (g_active_jobs[i].status == JOB_STATUS_IDLE) {
			return &g_active_jobs[i];
         }
    }
return NULL;
}

/**
* @brief Выполняет текущий шаг задания.
* @param job Указатель на контекст задания.
*/
static void JobManager_ExecuteStep(JobContext_t* job)
{
	const ProcessStep_t* current_step = &job->current_recipe[job->current_step_index];
    // Проверяем, не достигли ли мы конца рецепта
    if (current_step->atomic_actions == NULL || current_step->num_actions == 0) {
    	JobManager_CompleteJob(job, JOB_STATUS_COMPLETED);
    	return;
    }

   job->step_start_time_ms = HAL_GetTick(); // Обновляем время начала шага
   // Количество ожидаемых ответов - это количество атомарных действий в текущем шаге
   job->pending_actions_count = current_step->num_actions;

   char info_msg[APP_USB_RESP_MAX_LEN];
   snprintf(info_msg, sizeof(info_msg), "INFO: Job #%lu: Executing step %u (%u actions).", job->job_id, job->current_step_index, current_step->num_actions);
   Dispatcher_SendUsbResponse(info_msg);

   // Диспетчеризация атомарных действий текущего шага
   for (int i = 0; i < current_step->num_actions; i++) {
	   const AtomicAction_t* action = &current_step->atomic_actions[i];
        // Placeholder for CAN_Message_t - will be defined in can_packer.h
        // Placeholder for can_msg - will be used after can_packer is implemented
        CAN_Message_t can_msg; // Эта переменная нужна, если вы реально отправляете CAN-сообщения
        switch (action->action) {
              case ACTION_ROTATE_MOTOR:
               // --- DEBUG-СООБЩЕНИЕ ---
            	  char motor_msg[APP_USB_RESP_MAX_LEN];
                  snprintf(motor_msg, sizeof(motor_msg), "DEBUG: Job #%lu: Sent ROTATE_MOTOR (ID:%u, Steps:%ld, Speed:%u) to Exec.",
                  job->job_id, action->params.rotate_motor.motor_id, action->params.rotate_motor.steps, action->params.rotate_motor.speed);
                  Dispatcher_SendUsbResponse(motor_msg);
                  // --- РЕАЛЬНАЯ ОТПРАВКА CAN (после реализации can_packer) ---
                  Packer_CreateRotateMotorMsg(action->params.rotate_motor.motor_id, action->params.rotate_motor.steps, action->params.rotate_motor.speed, job->job_id, &can_msg);
                  xQueueSend(can_tx_queue_handle, &can_msg, 0);
                  break;

               case ACTION_START_PUMP:
			   // --- DEBUG-СООБЩЕНИЕ ---
                  char pump_start_msg[APP_USB_RESP_MAX_LEN];
				  snprintf(pump_start_msg, sizeof(pump_start_msg), "DEBUG: Job #%lu: Sent START_PUMP (ID:%u) to Exec.", job->job_id, action->params.pump.pump_id);
                  Dispatcher_SendUsbResponse(pump_start_msg);
                  // --- РЕАЛЬНАЯ ОТПРАВКА CAN ---
                  Packer_CreateStartPumpMsg(action->params.pump.pump_id, job->job_id, &can_msg);
                  xQueueSend(can_tx_queue_handle, &can_msg, 0);
                  break;

               case ACTION_STOP_PUMP:
                // --- DEBUG-СООБЩЕНИЕ ---
                   char pump_stop_msg[APP_USB_RESP_MAX_LEN];
                   snprintf(pump_stop_msg, sizeof(pump_stop_msg), "DEBUG: Job #%lu: Sent STOP_PUMP (ID:%u) to Exec.", job->job_id, action->params.pump.pump_id);
                   Dispatcher_SendUsbResponse(pump_stop_msg);
                   // --- РЕАЛЬНАЯ ОТПРАВКА CAN ---
                   Packer_CreateStopPumpMsg(action->params.pump.pump_id, job->job_id, &can_msg);
                   xQueueSend(can_tx_queue_handle, &can_msg, 0);
                   break;

                case ACTION_HOME_MOTOR:
                 // --- DEBUG-СООБЩЕНИЕ ---
                    char home_msg[APP_USB_RESP_MAX_LEN];
                    snprintf(home_msg, sizeof(home_msg), "DEBUG: Job #%lu: Sent HOME_MOTOR (ID:%u, Speed:%u) to Exec.",
                    job->job_id, action->params.home_motor.motor_id, action->params.home_motor.speed);
                    Dispatcher_SendUsbResponse(home_msg);
                    // --- РЕАЛЬНАЯ ОТПРАВКА CAN ---
                    Packer_CreateHomeMotorMsg(action->params.home_motor.motor_id, action->params.home_motor.speed, job->job_id, &can_msg);
                    xQueueSend(can_tx_queue_handle, &can_msg, 0);
                    break;

                 case ACTION_WAIT_MS:
                    // --- DEBUG-СООБЩЕНИЕ ---
                    char wait_msg[APP_USB_RESP_MAX_LEN];
                    snprintf(wait_msg, sizeof(wait_msg), "DEBUG: Job #%lu: Started WAIT_MS for %lu ms.", job->job_id, action->params.wait.delay_ms);
                    Dispatcher_SendUsbResponse(wait_msg);
                    // --- ВНУТРЕННЯЯ ЛОГИКА ---
                    // ACTION_WAIT_MS обрабатывается JobManager_Run() по таймауту.
                    // Оно не требует отправки в CAN или ответа от исполнителя.
                    //pending_actions_count здесь уменьшается, так как JobManager_Run() возьмет на себя "завершение" этого действия после истечения таймера.
                    job->pending_actions_count--;
                    break;


                 default:
                    char err_msg_action[APP_USB_RESP_MAX_LEN];
                    snprintf(err_msg_action, sizeof(err_msg_action), "ERROR: Job #%lu: Unknown action %d in step %u.", job->job_id, action->action, job->current_step_index);
                    Dispatcher_SendUsbResponse(err_msg_action);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
            }
        }

// Если после диспетчеризации всех действий шага pending_actions_count стал 0,
// значит все действия в этом шаге были внутренними (например, только ACTION_WAIT_MS).
// В этом случае сразу переходим к следующему шагу.
     if (job->pending_actions_count == 0) {
         job->current_step_index++;
         JobManager_ExecuteStep(job);
     }
}

/**
* @brief Завершает задание и освобождает слот.
* @param job Указатель на контекст задания.
* @param final_status Финальный статус задания.
*/
static void JobManager_CompleteJob(JobContext_t* job, JobStatus_t final_status)
{
	job->status = final_status;
    char final_msg[APP_USB_RESP_MAX_LEN];
    snprintf(final_msg, sizeof(final_msg), "INFO: Job #%lu finished with status %d.", job->job_id, final_status);
    Dispatcher_SendUsbResponse(final_msg);

    // Если это была инициализация системы, и она успешна, обновляем статус системы
    if (job->initial_recipe_id == RECIPE_INITIALIZE_SYSTEM && final_status == JOB_STATUS_COMPLETED) {

    // Здесь потребуется механизм обновления g_system_state в task_dispatcher.
    // Для этого нужна либо callback-функция, либо глобальный семафор/флаг.
    // Пока мы не реализуем это, g_system_state в task_dispatcher будет висеть в INITIALIZING
    // Мы используем специальную функцию для этого.
         JobManager_SignalSystemReady();
     }

    // Освобождаем слот для нового задания
    job->status = JOB_STATUS_IDLE;
    job->job_id = 0; // Сбрасываем ID, чтобы избежать случайного совпадения
}

/**
* @brief Механизм для сигнализации task_dispatcher о завершении инициализации системы.
*        В реальном проекте, это может быть сделано через FreeRTOS Event Group или
*        Task Notification для задачи task_dispatcher.
*        Сейчас это заглушка.
*/
static void JobManager_SignalSystemReady(void) {
// Здесь нужен FreeRTOS механизм
// Например:
// xEventGroupSetBits(g_system_event_group, SYSTEM_EVENT_INITIALIZED);
// xTaskNotifyGive(task_dispatcher_handle);
Dispatcher_SendUsbResponse("DEBUG: Signaling system READY.");
}
