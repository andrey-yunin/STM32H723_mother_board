/*
 * job_manager.c
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#include "Dispatcher/job_manager.h"
#include "Dispatcher/recipe_store.h"
#include "Dispatcher/recipe_executor_action_builder.h"
#include "Dispatcher/executor_transaction.h"
#include "Dispatcher/executor_command_tx.h"
#include "Dispatcher/dispatcher_io.h"
#include "Dispatcher/can_response_router.h"
#include "Dispatcher/service_manager.h"
#include "shared_resources.h"
#include "app_config.h"
#include "task_dispatcher.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "main.h" // Для HAL_GetTick()

// --- Внутренние типы данных ---

typedef enum {
    JOB_STATUS_IDLE = 0,
    JOB_STATUS_RUNNING = 1,
    JOB_STATUS_COMPLETED = 3,
    JOB_STATUS_TIMEOUT = 4,
    JOB_STATUS_ERROR = 5
} JobStatus_t;

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
    RecipeID_t initial_recipe_id;
    const ProcessStep_t* current_recipe;
    uint8_t current_step_index;
    uint8_t pending_actions_count;
    uint32_t step_start_time_ms;
    uint32_t step_timeout_ms;
    ExecutorTransactionTable_t transactions;
    UniversalCommand_t initial_cmd;
    JobInternalAction_t internal_actions[JOB_MAX_INTERNAL_ACTIONS];
} JobContext_t;

// --- Внутренние переменные ---
static JobContext_t g_active_jobs[MAX_CONCURRENT_JOBS];
static uint32_t g_next_job_id = 1;

// --- Прототипы внутренних функций ---
static JobContext_t* JobManager_FindFreeSlot(void);
static void JobManager_ExecuteStep(JobContext_t* job);
static void JobManager_CompleteJob(
		JobContext_t* job,
		JobStatus_t final_status,
		uint16_t host_status_code);
static void JobManager_SignalSystemReady(void);

static void JobManager_ResetTransactions(JobContext_t* job);
static bool JobManager_TickElapsed(uint32_t start_ms, uint32_t timeout_ms);
static bool JobManager_CompleteCurrentAction(JobContext_t* job, const char* source_label);
static bool JobManager_StageExecutorCommand(
		JobContext_t* job,
		ExecutorCommandTxCommand_t* staged_commands,
		uint8_t* staged_count,
		const RecipeExecutorAction_t* executor_action);
static bool JobManager_BuildAndStageExecutorAction(
        JobContext_t* job,
        const AtomicAction_t* action,
        ExecutorCommandTxCommand_t* staged_commands,
        uint8_t* staged_count);
static JobContext_t* JobManager_FindJobForRoutedResponse(
		const CanRoutedResponse_t* routed,
		bool require_channel);
static void JobManager_LogUnexpectedRoutedResponse(const CanRoutedResponse_t* routed, const char* reason);
static void JobManager_HandleExecutorAck(const CanRoutedResponse_t* routed);
static void JobManager_HandleExecutorNack(const CanRoutedResponse_t* routed);
static void JobManager_HandleExecutorData(const CanRoutedResponse_t* routed);
static void JobManager_HandleExecutorDone(const CanRoutedResponse_t* routed);
static void JobManager_HandleExecutorLog(const CAN_Response_t* response);
static bool JobManager_CheckTransactionTimeouts(JobContext_t* job);

static void JobManager_ResetInternalActions(JobContext_t* job);
static JobInternalAction_t* JobManager_RegisterInternalAction(
		JobContext_t* job,
        JobInternalActionType_t type,
        uint32_t duration_ms);
static bool JobManager_CheckInternalActions(JobContext_t* job);
static ExecutorTransactionResponsePolicy_t JobManager_ResponsePolicyFromExecutorAction(ExecutorActionResponsePolicy_t policy);
static uint16_t JobManager_MapExecutorNackToHostError(const ExecutorTransactionUpdate_t* update);
static uint16_t JobManager_MapExecutorTxStatusToHostError(ExecutorCommandTxStatus_t status);

/*
 * Расширяет timeout текущего шага recipe до требуемого значения.
 * Используется finite-действиями, физическое время которых больше
 * базового JOB_TIMEOUT_MS.
 */
static void JobManager_ExtendStepTimeout(JobContext_t* job, uint32_t required_timeout_ms)
{
	if (required_timeout_ms > job->step_timeout_ms) {
		job->step_timeout_ms = required_timeout_ms;
		}
}

static ExecutorTransactionResponsePolicy_t JobManager_ResponsePolicyFromExecutorAction(ExecutorActionResponsePolicy_t policy)
{
    switch (policy) {
        case EXECUTOR_ACTION_RESPONSE_DATA_THEN_DONE:
            return EXECUTOR_TRANSACTION_RESPONSE_DATA_THEN_DONE;

        case EXECUTOR_ACTION_RESPONSE_MULTI_DATA_THEN_DONE:
            return EXECUTOR_TRANSACTION_RESPONSE_MULTI_DATA_THEN_DONE;

        case EXECUTOR_ACTION_RESPONSE_DONE_ONLY:
        default:
            return EXECUTOR_TRANSACTION_RESPONSE_DONE_ONLY;
    }
}

static uint16_t JobManager_MapExecutorNackToHostError(const ExecutorTransactionUpdate_t* update)
{
	if (update == NULL) {
		return HOST_ERR_GENERAL;
	}

	switch (update->error_code) {
		case CAN_NACK_ERR_UNKNOWN_CMD:
			return HOST_ERR_UNKNOWN_CMD;

		case CAN_NACK_ERR_INVALID_DEVICE_ID:
		case CAN_NACK_ERR_INVALID_KEY:
		case CAN_NACK_ERR_INVALID_PARAM:
			return HOST_ERR_INVALID_PARAM;

		case CAN_NACK_ERR_DEVICE_BUSY:
			if (update->node_id == CAN_ADDR_THERMO_BOARD &&
					(update->low_command_code == CAN_CMD_THERMO_GET_TEMP ||
							update->low_command_code == CAN_CMD_THERMO_GET_ALL)) {
				return HOST_ERR_HARDWARE;
			}
			return HOST_ERR_BUSY;

		case CAN_NACK_ERR_FLASH_WRITE:
			return HOST_ERR_HARDWARE;

		case CAN_NACK_ERR_THERMO_BUSY:
			return HOST_ERR_BUSY;

		default:
			return HOST_ERR_GENERAL;
	}
}

static uint16_t JobManager_MapExecutorTxStatusToHostError(ExecutorCommandTxStatus_t status)
{
	switch (status) {
		case EXECUTOR_COMMAND_TX_DUPLICATE_TRANSACTION:
		case EXECUTOR_COMMAND_TX_QUEUE_FULL:
			return HOST_ERR_BUSY;

		case EXECUTOR_COMMAND_TX_INVALID_ARG:
		case EXECUTOR_COMMAND_TX_INVALID_FRAME:
		case EXECUTOR_COMMAND_TX_ROUTE_REGISTRATION_FAILED:
		case EXECUTOR_COMMAND_TX_QUEUE_SEND_FAILED:
		default:
			return HOST_ERR_GENERAL;
	}
}

static bool JobManager_BuildAndStageExecutorAction(
        JobContext_t* job,
        const AtomicAction_t* action,
        ExecutorCommandTxCommand_t* staged_commands,
        uint8_t* staged_count)
{
    RecipeExecutorActionContext_t action_ctx = {
        .job_id = job->job_id,
        .recipe_id = job->initial_recipe_id,
        .initial_cmd = &job->initial_cmd,
        .current_step_timeout_ms = job->step_timeout_ms
    };
    RecipeExecutorAction_t executor_action;
    char info_msg[APP_USB_RESP_MAX_LEN];
    info_msg[0] = '\0';

    if (!RecipeExecutorActionBuilder_Build(
            &action_ctx,
            action,
            &executor_action,
            info_msg,
            sizeof(info_msg))) {
        if (info_msg[0] == '\0') {
            snprintf(info_msg, sizeof(info_msg),
                    "ERROR: Job #%lu: Cannot build executor action %d.",
                    (unsigned long)job->job_id,
                    (int)action->action);
        }
        Dispatcher_SendUsbResponse(info_msg);
        JobManager_CompleteJob(job, JOB_STATUS_ERROR, HOST_ERR_INVALID_PARAM);
        return false;
    }

    if (!executor_action.command_required) {
        if (job->pending_actions_count > 0U) {
            job->pending_actions_count--;
        }
        return true;
    }

    JobManager_ExtendStepTimeout(job, executor_action.operation_timeout_ms);

    switch (action->action) {
        case ACTION_ROTATE_MOTOR:
            snprintf(info_msg, sizeof(info_msg),
                    "DEBUG: Job #%lu: Queued ROTATE_MOTOR (Phys:%u:%u, Steps:%ld, Speed:%u)",
                    (unsigned long)job->job_id,
                    executor_action.node_id,
                    executor_action.channel,
                    (long)executor_action.debug_signed_value,
                    (unsigned int)executor_action.debug_value);
            break;

        case ACTION_HOME_MOTOR:
            snprintf(info_msg, sizeof(info_msg),
                    "DEBUG: Job #%lu: Queued HOME_MOTOR (Phys:%u:%u, Speed:%u)",
                    (unsigned long)job->job_id,
                    executor_action.node_id,
                    executor_action.channel,
                    (unsigned int)executor_action.debug_value);
            break;

        case ACTION_RUN_PUMP_DURATION:
            snprintf(info_msg, sizeof(info_msg),
                    "DEBUG: Job #%lu: Queued RUN_PUMP_DURATION (Phys:%u:%u, Duration:%lu)",
                    (unsigned long)job->job_id,
                    executor_action.node_id,
                    executor_action.channel,
                    (unsigned long)executor_action.debug_value);
            break;

        default:
            snprintf(info_msg, sizeof(info_msg),
                    "DEBUG: Job #%lu: Queued %s (Phys:%u:%u)",
                    (unsigned long)job->job_id,
                    executor_action.action_label,
                    executor_action.node_id,
                    executor_action.channel);
            break;
    }
    Dispatcher_SendUsbResponse(info_msg);

    return JobManager_StageExecutorCommand(
            job,
            staged_commands,
            staged_count,
            &executor_action);
}

/*
 * Проверяет истечение timeout с учетом переполнения HAL_GetTick().
 * Все будущие ACK timeout и operation timeout проверки должны идти через эту функцию,
 * чтобы поведение оставалось корректным после wrap-around системного тика.
 */
static bool JobManager_TickElapsed(uint32_t start_ms, uint32_t timeout_ms)
{
	return (uint32_t)(HAL_GetTick() - start_ms) >= timeout_ms;
}

/*
 * Очищает таблицу low-level executor transactions внутри Host job.
 * Вызывается при старте job/step и при завершении job, чтобы late responses
 * не могли совпасть со старым transaction context.
 */
static void JobManager_ResetTransactions(JobContext_t* job)
{
	if (job == NULL) {
		return;
		}
	ExecutorTransactionTable_ResetOperation(
            &job->transactions,
            TX_OWNER_HOST_OPERATION,
            job->job_id);
	JobManager_ResetInternalActions(job);
}


/*
  * Завершает один action текущего recipe step.
  * Это внутреннее completion-событие JobManager, а не executor DONE.
  */
 static bool JobManager_CompleteCurrentAction(JobContext_t* job, const char* source_label)
 {
     if (job == NULL || job->status != JOB_STATUS_RUNNING) {
         return false;
     }

     if (job->pending_actions_count == 0U) {
         char warn_msg[APP_USB_RESP_MAX_LEN];
         snprintf(warn_msg, sizeof(warn_msg),
                  "WARNING: Job #%lu: Duplicate action completion from %s at step %u.",
                  (unsigned long)job->job_id,
                  source_label,
                  (unsigned int)job->current_step_index);
         Dispatcher_SendUsbResponse(warn_msg);
         return false;
     }

     job->pending_actions_count--;

     if (job->pending_actions_count == 0U) {
         job->current_step_index++;
         JobManager_ExecuteStep(job);
     }

     return true;
 }

 /*
  * Складывает подготовленный CAN frame в staging-буфер текущего recipe step.
  * Физическая отправка выполняется только после успешной подготовки всех actions
  * шага, чтобы параллельный step стартовал атомарно на уровне Дирижера.
  */
static bool JobManager_StageExecutorCommand(
         JobContext_t* job,
         ExecutorCommandTxCommand_t* staged_commands,
         uint8_t* staged_count,
         const RecipeExecutorAction_t* executor_action)
 {
     if (job == NULL || staged_commands == NULL ||
             staged_count == NULL || executor_action == NULL) {
         return false;
     }

     if (*staged_count >= JOB_MAX_EXECUTOR_TRANSACTIONS) {
         char err_msg[APP_USB_RESP_MAX_LEN];
         snprintf(err_msg, sizeof(err_msg),
                  "ERROR: Job #%lu: Too many CAN actions in step while staging %s.",
                  (unsigned long)job->job_id,
                  executor_action->action_label);
         Dispatcher_SendUsbResponse(err_msg);
         JobManager_CompleteJob(job, JOB_STATUS_ERROR, HOST_ERR_GENERAL);
         return false;
     }

     ExecutorCommandTxCommand_t* command = &staged_commands[*staged_count];
     memset(command, 0, sizeof(*command));

     command->frame = executor_action->can_msg;
     command->transaction.route_owner = TX_OWNER_HOST_OPERATION;
     command->transaction.operation_id = job->job_id;
     command->transaction.host_command_code = job->initial_cmd.command_code;
     command->transaction.low_command_code = executor_action->low_command_code;
     command->transaction.expected_node_id = executor_action->node_id;
     command->transaction.expected_channel = executor_action->channel;
     command->transaction.channel_valid = executor_action->channel_valid;
     command->transaction.response_policy =
             JobManager_ResponsePolicyFromExecutorAction(executor_action->response_policy);
     command->transaction.operation_timeout_ms = executor_action->operation_timeout_ms;

     (*staged_count)++;
     return true;
 }

 /*
  * Находит running job и transaction, соответствующие routed CAN response.
  * Это финальная защита JobManager от late/service/foreign responses.
 */
 static JobContext_t* JobManager_FindJobForRoutedResponse(
         const CanRoutedResponse_t* routed,
		 bool require_channel)
 {
     if (routed == NULL) {
         return NULL;
     }

     for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
         JobContext_t* job = &g_active_jobs[i];

         if (job->status != JOB_STATUS_RUNNING) {
             continue;
         }

         if (routed->context_job_id != 0U &&
             job->job_id != routed->context_job_id) {
             continue;
         }

         if (ExecutorTransactionTable_HasMatching(
                 &job->transactions,
                 routed,
                 require_channel)) {
             return job;
         }
     }

     return NULL;
 }


 /*
  * Логирует routed response, который не совпал ни с одной активной transaction.
  * Такой response не имеет права продвигать Host job.
  */
 static void JobManager_LogUnexpectedRoutedResponse(const CanRoutedResponse_t* routed, const char* reason)
 {
     if (routed == NULL) {
         return;
     }

     const CAN_Response_t* response = &routed->parsed;
     char msg[APP_USB_RESP_MAX_LEN];

     snprintf(msg, sizeof(msg),
              "WARNING: Unexpected CAN response ignored: src=0x%02X type=%u sub=%u ctx=%s0x%04X (%s).",
              response->source_addr,
              response->msg_type,
              response->sub_type,
              routed->context_valid ? "" : "invalid:",
              routed->context_valid ? routed->context_command_code : 0U,
              reason);

     Dispatcher_SendUsbResponse(msg);
 }

/*
 * Обрабатывает executor ACK как command-level подтверждение.
 *
 * ACK не несет channel, поэтому при параллельных каналах одной команды
 * он подтверждает все active transactions того же job + node + command.
 * Action при этом не завершается: завершение делает только DATA/DONE
 * согласно response_policy transaction-а.
 */
static void JobManager_HandleExecutorAck(const CanRoutedResponse_t* routed)
{
	if (routed == NULL || !routed->context_valid) {
		JobManager_LogUnexpectedRoutedResponse(routed, "invalid context for ACK");
		return;
	}

	bool matched = false;
	const char* no_match_reason = "no matching transaction for ACK";

	for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
		JobContext_t* job = &g_active_jobs[i];

		if (job->status != JOB_STATUS_RUNNING) {
			continue;
		}

		if (routed->context_job_id != 0U &&
				job->job_id != routed->context_job_id) {
			continue;
		}

		ExecutorTransactionUpdate_t update =
				ExecutorTransactionTable_HandleAck(&job->transactions, routed);
		if (update.matched) {
			matched = true;
		}
		else if (update.reason != NULL) {
			no_match_reason = update.reason;
		}
	}

	if (!matched) {
		JobManager_LogUnexpectedRoutedResponse(routed, no_match_reason);
	}
}

/*
 * Обрабатывает executor NACK как command-level отказ.
 *
 * NACK также не несет channel: если исполнитель отказал node + command,
 * все активные transactions этого command-а считаются завершенными ошибкой,
 * а Host job закрывается ERROR.
 */
static void JobManager_HandleExecutorNack(const CanRoutedResponse_t* routed)
{
	if (routed == NULL || !routed->context_valid) {
		JobManager_LogUnexpectedRoutedResponse(routed, "invalid context for NACK");
		return;
	}

	bool matched = false;
	const char* no_match_reason = "no matching transaction for NACK";

	for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
		JobContext_t* job = &g_active_jobs[i];

		if (job->status != JOB_STATUS_RUNNING) {
			continue;
		}

		if (routed->context_job_id != 0U &&
				job->job_id != routed->context_job_id) {
			continue;
		}

		ExecutorTransactionUpdate_t update =
				ExecutorTransactionTable_HandleNack(&job->transactions, routed);
		if (update.matched) {
			matched = true;

			char msg[APP_USB_RESP_MAX_LEN];
			snprintf(msg, sizeof(msg),
					"ERROR: Executor NACK matched: job=%lu node=0x%02X cmd=0x%04X err=0x%04X.",
					(unsigned long)job->job_id,
					update.node_id,
					update.low_command_code,
					update.error_code);
			Dispatcher_SendUsbResponse(msg);

			JobManager_CompleteJob(
					job,
					JOB_STATUS_ERROR,
					JobManager_MapExecutorNackToHostError(&update));
		}
		else if (update.reason != NULL) {
			no_match_reason = update.reason;
		}
	}

	if (!matched) {
		JobManager_LogUnexpectedRoutedResponse(routed, no_match_reason);
	}
}

/*
 * Обрабатывает DATA только для transaction, policy которой допускает DATA.
 *
 * DATA должен иметь однозначный route context. Если executor прислал DATA
 * без предварительного ACK, DATA считается implicit ACK: transaction переходит
 * из SENT сразу в DATA_SEEN после проверки policy.
 */
static void JobManager_HandleExecutorData(const CanRoutedResponse_t* routed)
{
	JobContext_t* job = JobManager_FindJobForRoutedResponse(
			routed,
			routed != NULL && routed->context_channel_valid);

	if (job == NULL) {
		JobManager_LogUnexpectedRoutedResponse(routed, "no matching transaction for DATA");
		return;
	}

	ExecutorTransactionUpdate_t update =
			ExecutorTransactionTable_HandleData(&job->transactions, routed);
	if (!update.matched) {
		JobManager_LogUnexpectedRoutedResponse(
				routed,
				update.reason != NULL ? update.reason : "no matching transaction for DATA");
		return;
	}

	if (update.event == EXECUTOR_TRANSACTION_EVENT_PROTOCOL_ERROR) {
		JobManager_LogUnexpectedRoutedResponse(routed, update.reason);
		JobManager_CompleteJob(job, JOB_STATUS_ERROR, HOST_ERR_GENERAL);
	}
}

/*
 * Обрабатывает DONE как transaction-level завершение.
 *
 * DONE несет low-level command + channel, поэтому закрывает ровно одну
 * transaction. После закрытия transaction уменьшается pending_actions_count;
 * recipe step продвигается только когда закрыты все actions шага.
 */
static void JobManager_HandleExecutorDone(const CanRoutedResponse_t* routed)
{
	JobContext_t* job = JobManager_FindJobForRoutedResponse(routed, true);

	if (job == NULL) {
		JobManager_LogUnexpectedRoutedResponse(routed, "no matching transaction for DONE");
		return;
	}

	ExecutorTransactionUpdate_t update =
			ExecutorTransactionTable_HandleDone(&job->transactions, routed);
	if (!update.matched) {
		JobManager_LogUnexpectedRoutedResponse(
				routed,
				update.reason != NULL ? update.reason : "no matching transaction for DONE");
		return;
	}

	if (update.event == EXECUTOR_TRANSACTION_EVENT_PROTOCOL_ERROR) {
		JobManager_LogUnexpectedRoutedResponse(routed, update.reason);
		JobManager_CompleteJob(job, JOB_STATUS_ERROR, HOST_ERR_GENERAL);
		return;
	}

	JobManager_CompleteCurrentAction(job, "executor transaction");
}

 /*
  * LOG не продвигает recipe и не закрывает transaction.
  * Это диагностический текст от executor.
  */
 static void JobManager_HandleExecutorLog(const CAN_Response_t* response)
 {
     if (response == NULL) {
         return;
     }

     char log_msg[APP_USB_RESP_MAX_LEN];
     snprintf(log_msg, sizeof(log_msg), "DEBUG: [0x%02X]: %s",
              response->source_addr,
              response->payload.log);
     Dispatcher_SendUsbResponse(log_msg);
 }

 /*
  * Проверяет ACK timeout и operation timeout по всем active transactions job-а.
  * ACK timeout и operation timeout различаются, потому что это разные классы отказов.
  */
 static bool JobManager_CheckTransactionTimeouts(JobContext_t* job)
 {
     if (job == NULL || job->status != JOB_STATUS_RUNNING) {
         return false;
     }

     ExecutorTransactionUpdate_t update;
     if (!ExecutorTransactionTable_CheckTimeouts(
             &job->transactions,
             HAL_GetTick(),
             &update)) {
         return false;
     }

     char msg[APP_USB_RESP_MAX_LEN];
     const char* timeout_label =
             (update.event == EXECUTOR_TRANSACTION_EVENT_ACK_TIMEOUT)
                     ? "ACK timeout"
                     : "operation timeout";

     snprintf(msg, sizeof(msg),
              "ERROR: Job #%lu %s: node=0x%02X cmd=0x%04X.",
              (unsigned long)job->job_id,
              timeout_label,
              update.node_id,
              update.low_command_code);
     Dispatcher_SendUsbResponse(msg);

     ServiceManager_StartRecovery(update.node_id);
     JobManager_CompleteJob(job, JOB_STATUS_TIMEOUT, HOST_ERR_TIMEOUT);
     return true;
}

/*
 * Сбрасывает internal actions текущего Host job.
 * Internal actions не имеют CAN route и завершаются условиями внутри JobManager.
 */
static void JobManager_ResetInternalActions(JobContext_t* job)
{
	if (job == NULL) {
		return;
		}
	memset(job->internal_actions, 0, sizeof(job->internal_actions));
}

/*
 * Регистрирует internal action текущего recipe step.
 * Сейчас единственный поддержанный тип - WAIT_MS.
 */
static JobInternalAction_t* JobManager_RegisterInternalAction(
		JobContext_t* job,
        JobInternalActionType_t type,
        uint32_t duration_ms)
{
	if (job == NULL) {
		return NULL;
		}
	for (uint8_t i = 0; i < JOB_MAX_INTERNAL_ACTIONS; i++) {
		JobInternalAction_t* action = &job->internal_actions[i];

		if (!action->active) {
			memset(action, 0, sizeof(*action));
            action->active = true;
            action->type = type;
            action->started_at_ms = HAL_GetTick();
            action->duration_ms = duration_ms;
            return action;
            }
		}
	return NULL;
}

/*
 * Проверяет completion внутренних actions.
 * Возвращает true, если action был закрыт и job мог перейти на следующий step.
 */
static bool JobManager_CheckInternalActions(JobContext_t* job)
{
	if (job == NULL || job->status != JOB_STATUS_RUNNING) {
		return false;
	}

	for (uint8_t i = 0; i < JOB_MAX_INTERNAL_ACTIONS; i++) {
		JobInternalAction_t* action = &job->internal_actions[i];

		if (!action->active) {
			continue;
			}

		switch (action->type) {
		case JOB_INTERNAL_ACTION_WAIT_MS:
			if (JobManager_TickElapsed(action->started_at_ms, action->duration_ms)) {
				action->active = false;
                return JobManager_CompleteCurrentAction(job, "WAIT_MS");
                }
			break;

		default:
			action->active = false;
			JobManager_CompleteJob(job, JOB_STATUS_ERROR, HOST_ERR_GENERAL);
			return true;
			}
		}
	return false;
}

// --- API функции ---

/*
 * Инициализирует runtime-состояние JobManager.
 * После вызова нет активных Host jobs, а счетчик job_id начинается заново.
 */
void JobManager_Init(void)
{
	for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
		g_active_jobs[i].status = JOB_STATUS_IDLE;
        g_active_jobs[i].job_id = 0;
        JobManager_ResetTransactions(&g_active_jobs[i]);
        }
	g_next_job_id = 1;
}

/*
 * Создает новую Host recipe operation.
 * Parser уже проверил Host packet и заполнил UniversalCommand_t;
 * здесь выбирается recipe, сохраняется Host context и запускается первый шаг.
 */
uint32_t JobManager_StartNewJob(const UniversalCommand_t* parsed_cmd)
{
	JobContext_t* job = JobManager_FindFreeSlot();

    if (job == NULL) {
    	Dispatcher_SendUsbResponse("ERROR: No free job slots to start new job.");
        Dispatcher_SendError(parsed_cmd->command_code, HOST_ERR_BUSY);
        return 0;
    }

    // Сохраняем ID до того, как он может быть обнулен в JobManager_CompleteJob
    const uint32_t new_job_id = g_next_job_id++;
    if (g_next_job_id == 0) g_next_job_id = 1;

    job->job_id = new_job_id;
    job->status = JOB_STATUS_RUNNING;
    JobManager_ResetTransactions(job);
    job->initial_recipe_id = parsed_cmd->recipe_id;
    job->current_recipe = Recipe_Get(parsed_cmd->recipe_id);
    if (job->current_recipe == NULL) {
         char err_msg[APP_USB_RESP_MAX_LEN];
         snprintf(err_msg, sizeof(err_msg), "ERROR: Job %lu: Unknown recipe ID %d.", (unsigned long)job->job_id, (int)parsed_cmd->recipe_id);
         Dispatcher_SendUsbResponse(err_msg);
         JobManager_CompleteJob(job, JOB_STATUS_ERROR, HOST_ERR_NOT_SUPPORTED);
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

/*
 * Основной polling-runner JobManager.
 * Обрабатывает routed executor responses, проверяет таймауты активных jobs
 * и продвигает recipe только через валидные internal/action completions.
 */

void JobManager_Run(void)
{
	CanRoutedResponse_t routed;

     while (xQueueReceive(can_job_rx_queue_handle, &routed, 0) == pdPASS) {
         const CAN_Response_t* response = &routed.parsed;

         switch (response->msg_type) {
             case CAN_MSG_TYPE_ACK:
                 JobManager_HandleExecutorAck(&routed);
                 break;

             case CAN_MSG_TYPE_NACK:
                 JobManager_HandleExecutorNack(&routed);
                 break;

             case CAN_MSG_TYPE_DATA_DONE_LOG:
                 switch (response->sub_type) {
                     case CAN_SUB_TYPE_DATA:
                         JobManager_HandleExecutorData(&routed);
                         break;

                     case CAN_SUB_TYPE_DONE:
                         JobManager_HandleExecutorDone(&routed);
                         break;

                     case CAN_SUB_TYPE_LOG:
                         JobManager_HandleExecutorLog(response);
                         break;

                     default:
                         JobManager_LogUnexpectedRoutedResponse(&routed, "unknown DATA/DONE/LOG subtype");
                         break;
                 }
                 break;

             default:
                 JobManager_LogUnexpectedRoutedResponse(&routed, "unknown CAN response type");
                 break;
         }
     }

     for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
         JobContext_t* job = &g_active_jobs[i];

         if (job->status != JOB_STATUS_RUNNING) {
             continue;
         }

         if (JobManager_CheckTransactionTimeouts(job)) {
             continue;
         }

         if (JobManager_CheckInternalActions(job)) {
               continue;
           }


         if (JobManager_TickElapsed(job->step_start_time_ms, job->step_timeout_ms)) {
             char err_msg[APP_USB_RESP_MAX_LEN];
             snprintf(err_msg, sizeof(err_msg),
                      "ERROR: Job #%lu timed out at step %u.",
                      (unsigned long)job->job_id,
                      (unsigned int)job->current_step_index);
             Dispatcher_SendUsbResponse(err_msg);
             JobManager_CompleteJob(job, JOB_STATUS_TIMEOUT, HOST_ERR_TIMEOUT);
             continue;
         }
      }
}


// --- Внутренние функции ---

/*
 * Ищет свободный слот для нового Host job.
 * Сейчас MAX_CONCURRENT_JOBS равен 1; функция оставлена как основа
 * для будущего расширения параллельных Host operations.
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

/*
 * Запускает текущий recipe step.
 *
 * Все CAN actions сначала валидируются, регистрируются в transaction/router
 * и складываются в staging-буфер. Физическая отправка начинается только после
 * успешной подготовки всего шага, чтобы параллельный step не стартовал
 * частично при ошибке регистрации одной из команд.
 */
static void JobManager_ExecuteStep(JobContext_t* job)
{
    const ProcessStep_t* current_step = &job->current_recipe[job->current_step_index];
    if (current_step->atomic_actions == NULL || current_step->num_actions == 0) {
        JobManager_CompleteJob(job, JOB_STATUS_COMPLETED, HOST_STATUS_OK);
        return;
    }

    job->step_start_time_ms = HAL_GetTick();
    job->step_timeout_ms = JOB_TIMEOUT_MS;
    job->pending_actions_count = current_step->num_actions;

    /*
     * Новый recipe step начинает новый набор low-level ожиданий.
     * Late responses от предыдущего шага не должны совпасть с текущим context.
     */
    JobManager_ResetTransactions(job);

    char info_msg[APP_USB_RESP_MAX_LEN];
    snprintf(info_msg, sizeof(info_msg),
            "INFO: Job #%lu: Executing step %u (%u actions).",
            (unsigned long)job->job_id,
            (unsigned int)job->current_step_index,
            (unsigned int)current_step->num_actions);
    Dispatcher_SendUsbResponse(info_msg);

    ExecutorCommandTxCommand_t staged_commands[JOB_MAX_EXECUTOR_TRANSACTIONS];
    uint8_t staged_command_count = 0U;

    for (int i = 0; i < current_step->num_actions; i++) {
        const AtomicAction_t* action = &current_step->atomic_actions[i];

        switch (action->action) {
            case ACTION_ROTATE_MOTOR:
            case ACTION_HOME_MOTOR:
            case ACTION_RUN_PUMP_DURATION:
            case ACTION_START_PUMP:
            case ACTION_STOP_PUMP: {
                if (!JobManager_BuildAndStageExecutorAction(
                        job,
                        action,
                        staged_commands,
                        &staged_command_count)) {
                    return;
                }
                break;
            }

            case ACTION_WAIT_MS: {
                if (action->params.wait.delay_ms_source != PARAM_SOURCE_STATIC) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: WAIT_MS supports only static delay source (%u).",
                            (unsigned long)job->job_id,
                            (unsigned int)action->params.wait.delay_ms_source);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR, HOST_ERR_GENERAL);
                    return;
                }

                uint32_t delay_ms = action->params.wait.delay_ms;

                if (JobManager_RegisterInternalAction(
                        job,
                        JOB_INTERNAL_ACTION_WAIT_MS,
                        delay_ms) == NULL) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: Cannot register WAIT_MS internal action (%lu ms).",
                            (unsigned long)job->job_id,
                            (unsigned long)delay_ms);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR, HOST_ERR_BUSY);
                    return;
                }

                JobManager_ExtendStepTimeout(
                        job,
                        delay_ms + JOB_INTERNAL_ACTION_TIMEOUT_MARGIN_MS);

                snprintf(info_msg, sizeof(info_msg),
                        "DEBUG: Job #%lu: Registered WAIT_MS internal action (%lu ms).",
                        (unsigned long)job->job_id,
                        (unsigned long)delay_ms);
                Dispatcher_SendUsbResponse(info_msg);
                break;
            }

            case ACTION_PERFORM_SCAN: {
                uint8_t sys_id = action->params.perform_scan.photometer_id;
                uint8_t mask = action->params.perform_scan.wavelength_mask;

                if (action->params.perform_scan.wavelength_mask_source ==
                        PARAM_SOURCE_PHOTOMETER_WAVELENGTH_MASK &&
                        job->initial_cmd.args_type == ARGS_TYPE_PARSED) {
                    mask = job->initial_cmd.args.photometer_scan_single.wavelength_mask;
                }

                snprintf(info_msg, sizeof(info_msg),
                        "ERROR: Job #%lu: ACTION_PERFORM_SCAN unsupported "
                        "(Photometer SysID:%u, WavelengthMask:0x%02X).",
                        (unsigned long)job->job_id,
                        sys_id,
                        mask);
                Dispatcher_SendUsbResponse(info_msg);

                JobManager_CompleteJob(job, JOB_STATUS_ERROR, HOST_ERR_NOT_SUPPORTED);
                return;
            }

            default:
                snprintf(info_msg, sizeof(info_msg),
                        "ERROR: Job #%lu: Unknown action %d",
                        (unsigned long)job->job_id,
                        (int)action->action);
                Dispatcher_SendUsbResponse(info_msg);
                JobManager_CompleteJob(job, JOB_STATUS_ERROR, HOST_ERR_GENERAL);
                return;
        }
    }

    ExecutorCommandTxStatus_t tx_status = ExecutorCommandTx_SubmitBatch(
            &job->transactions,
            staged_commands,
            staged_command_count);
    if (tx_status != EXECUTOR_COMMAND_TX_OK) {
        snprintf(info_msg, sizeof(info_msg),
                "ERROR: Job #%lu: Executor command TX failed: %s.",
                (unsigned long)job->job_id,
                ExecutorCommandTx_StatusString(tx_status));
        Dispatcher_SendUsbResponse(info_msg);
        JobManager_CompleteJob(
				job,
				JOB_STATUS_ERROR,
				JobManager_MapExecutorTxStatusToHostError(tx_status));
        return;
    }

    if (job->pending_actions_count == 0U) {
        job->current_step_index++;
        JobManager_ExecuteStep(job);
    }
}


/*
 * Завершает Host job и отправляет Host DONE.
 * Executor DONE не должен попадать сюда напрямую: сначала он должен закрыть
 * соответствующую executor transaction или внутренний action.
 */
static void JobManager_CompleteJob(
		JobContext_t* job,
		JobStatus_t final_status,
		uint16_t host_status_code)
{
	job->status = final_status;
    char final_msg[APP_USB_RESP_MAX_LEN];
    snprintf(final_msg, sizeof(final_msg), "INFO: Job #%lu finished with status %d.", (unsigned long)job->job_id, final_status);
    Dispatcher_SendUsbResponse(final_msg);

    // Отправляем бинарный DONE-ответ.
    uint16_t done_status_code = host_status_code;
    if (final_status == JOB_STATUS_COMPLETED) {
		done_status_code = HOST_STATUS_OK;
	}
	else if (done_status_code == HOST_STATUS_OK) {
		done_status_code = HOST_ERR_GENERAL;
    }
    Dispatcher_SendDone(job->initial_cmd.command_code, done_status_code);


    if (job->initial_recipe_id == RECIPE_INITIALIZE_SYSTEM && final_status == JOB_STATUS_COMPLETED) {
         JobManager_SignalSystemReady();
    }

    job->status = JOB_STATUS_IDLE;
    JobManager_ResetTransactions(job);

    job->job_id = 0;
}

/*
 * Переводит глобальное состояние системы в READY после успешного INIT recipe.
 * Это Host/system-level состояние Дирижера, не low-level executor transaction.
 */
static void JobManager_SignalSystemReady(void) {
    Dispatcher_SendUsbResponse("DEBUG: Signaling system READY.");
	SetSystemReady();
}
