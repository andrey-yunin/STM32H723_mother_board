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
#include "Dispatcher/can_response_router.h"
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

static bool JobManager_IsTerminalTransactionState(ExecutorTransactionState_t state);
static void JobManager_ResetTransactions(JobContext_t* job);
static bool JobManager_HasDuplicateActiveTransaction(
		const JobContext_t* job,
		uint16_t low_command_code,
		uint8_t expected_node_id,
		uint8_t expected_channel,
		bool channel_valid);
static ExecutorTransaction_t* JobManager_RegisterExecutorTransaction(
		JobContext_t* job,
        uint16_t low_command_code,
        uint8_t expected_node_id,
        uint8_t expected_channel,
        bool channel_valid,
        JobResponsePolicy_t response_policy,
        uint32_t operation_timeout_ms);
static ExecutorTransaction_t* JobManager_FindMatchingTransaction(
		JobContext_t* job,
		const CanRoutedResponse_t* routed,
		bool require_channel);
static bool JobManager_TickElapsed(uint32_t start_ms, uint32_t timeout_ms);
static bool JobManager_CompleteCurrentAction(JobContext_t* job, const char* source_label);
static bool JobManager_StageCanTx(
		JobContext_t* job,
		CAN_Message_t* staged_msgs,
		uint8_t* staged_count,
		const CAN_Message_t* msg,
		const char* action_label);
static JobContext_t* JobManager_FindJobForRoutedResponse(
		const CanRoutedResponse_t* routed,
		ExecutorTransaction_t** out_tx,
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

/*
 * Возвращает модуль signed steps как uint32_t.
 * Отдельная функция нужна из-за INT32_MIN: его нельзя безопасно
 * инвертировать простым выражением -steps.
 */
static uint32_t JobManager_AbsSteps(int32_t steps)
{
	// INT32_MIN нельзя безопасно инвертировать простым "-steps".
    return (steps < 0) ? ((uint32_t)(-(steps + 1)) + 1) : (uint32_t)steps;
}

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

/*
 * Считает operation timeout для low-level Motion ROTATE.
 * Дирижер ждет расчетное время движения плюс запас, а не только
 * фиксированный базовый timeout шага.
 */
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
 * Единая проверка terminal-состояний executor transaction.
 * Такие записи уже не должны участвовать в duplicate/matching/timeout логике.
 */
static bool JobManager_IsTerminalTransactionState(ExecutorTransactionState_t state)
{
	return state == EXEC_TX_DONE ||
			state == EXEC_TX_NACK ||
			state == EXEC_TX_ACK_TIMEOUT ||
			state == EXEC_TX_OPERATION_TIMEOUT;
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
	CanResponseRouter_CloseJob(job->job_id);
	JobManager_ResetInternalActions(job);
	memset(job->transactions, 0, sizeof(job->transactions));
}

/*
 * Проверяет, не регистрируем ли мы второй раз ту же самую low-level transaction.
 *
 * Это НЕ запрет параллельной работы с одним NodeID:
 * один executor node может обслуживать разные каналы или разные low-level команды.
 *
 * Запрещаем только неоднозначные дубли:
 * - тот же node_id;
 * - та же low-level command;
 * - тот же channel, если channel участвует в корреляции;
 * - command без валидного channel, потому что такой ответ нельзя надежно
 *   отличить от уже активной transaction.
 */
static bool JobManager_HasDuplicateActiveTransaction(
		const JobContext_t* job,
		uint16_t low_command_code,
		uint8_t expected_node_id,
		uint8_t expected_channel,
		bool channel_valid)
{
	if (job == NULL) {
		return false;
		}

	for (uint8_t i = 0; i < JOB_MAX_EXECUTOR_TRANSACTIONS; i++) {
		const ExecutorTransaction_t* tx = &job->transactions[i];

		if (!tx->active) {
			continue;
			}

		if (JobManager_IsTerminalTransactionState(tx->state)) {
			continue;
			}

		if (tx->expected_node_id != expected_node_id) {
			continue;
			}

		if (tx->low_command_code != low_command_code) {
			continue;
			}

		if (!tx->channel_valid || !channel_valid) {
			return true;
			}

		if (tx->expected_channel == expected_channel) {
			return true;
			}
		}
	return false;
}

/*
 * Регистрирует ожидаемую low-level executor transaction.
 *
 * Эта запись является промышленной точкой корреляции:
 * только response, совпавший с node_id + low_command_code + channel,
 * сможет продвинуть Host job или recipe step.
 *
 * Возвращает NULL, если:
 * - job некорректен;
 * - уже есть активная transaction с тем же node + command + channel context;
 * - таблица transactions заполнена.
 */
static ExecutorTransaction_t* JobManager_RegisterExecutorTransaction(
		JobContext_t* job,
        uint16_t low_command_code,
        uint8_t expected_node_id,
        uint8_t expected_channel,
        bool channel_valid,
        JobResponsePolicy_t response_policy,
        uint32_t operation_timeout_ms)
{
	if (job == NULL) {
		return NULL;
	}

	if (JobManager_HasDuplicateActiveTransaction(
			job,
			low_command_code,
			expected_node_id,
			expected_channel,
			channel_valid)) {
		return NULL;
	}

	for (uint8_t i = 0; i < JOB_MAX_EXECUTOR_TRANSACTIONS; i++) {
		ExecutorTransaction_t* tx = &job->transactions[i];

		if (!tx->active) {
			memset(tx, 0, sizeof(*tx));

			tx->active = true;
            tx->job_id = job->job_id;

            // Host-команда, по которой Дирижер позже отправит Host DATA/DONE.
            tx->host_command_code = job->initial_cmd.command_code;

            // Low-level команда, по которой Executor вернет ACK/DATA/DONE/NACK.
            tx->low_command_code = low_command_code;

            tx->expected_node_id = expected_node_id;
            tx->expected_channel = expected_channel;
            tx->channel_valid = channel_valid;

            tx->response_policy = response_policy;
            tx->state = EXEC_TX_SENT;

            tx->sent_time_ms = HAL_GetTick();
            tx->ack_timeout_ms = JOB_ACK_TIMEOUT_MS;
            tx->operation_timeout_ms = operation_timeout_ms;

            tx->last_error_code = 0U;
            tx->data_count = 0U;

            if (!CanResponseRouter_Register(TX_OWNER_HOST_OPERATION,
					expected_node_id,
					low_command_code,
					expected_channel,
					channel_valid,
					job->job_id,
					tx->host_command_code)) {
                memset(tx, 0, sizeof(*tx));
                return NULL;
            }

            return tx;
            }
		}
	return NULL;
}

/*
 * Ищет transaction, которой принадлежит входящий routed CAN response.
 *
 * Проверяется:
 * - source NodeID исполнителя;
 * - active command context из router-а;
 * - channel, если caller требует transaction-level match.
 *
 * Если channel не требуется и найдено больше одной подходящей transaction,
 * response считается неоднозначным и не продвигает job.
 */
static ExecutorTransaction_t* JobManager_FindMatchingTransaction(
		JobContext_t* job,
		const CanRoutedResponse_t* routed,
		bool require_channel)
{
	if (job == NULL || routed == NULL || !routed->context_valid) {
		return NULL;
	}

	const CAN_Response_t* response = &routed->parsed;
	ExecutorTransaction_t* found = NULL;
	bool check_channel = require_channel || routed->context_channel_valid;
	uint8_t response_channel = routed->context_channel_valid
			? routed->context_channel
			: response->ch_idx;

	for (uint8_t i = 0; i < JOB_MAX_EXECUTOR_TRANSACTIONS; i++) {
		ExecutorTransaction_t* tx = &job->transactions[i];

		if (!tx->active) {
			continue;
			}

		if (tx->expected_node_id != response->source_addr) {
			continue;
			}

		if (tx->low_command_code != routed->context_command_code) {
			continue;
			}

		if (check_channel && tx->channel_valid &&
				tx->expected_channel != response_channel) {
			continue;
			}

		if (found != NULL) {
			return NULL;
			}

		found = tx;
		}
	return found;
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
 static bool JobManager_StageCanTx(
         JobContext_t* job,
         CAN_Message_t* staged_msgs,
         uint8_t* staged_count,
         const CAN_Message_t* msg,
         const char* action_label)
 {
     if (job == NULL || staged_msgs == NULL || staged_count == NULL || msg == NULL) {
         return false;
     }

     if (*staged_count >= JOB_MAX_EXECUTOR_TRANSACTIONS) {
         char err_msg[APP_USB_RESP_MAX_LEN];
         snprintf(err_msg, sizeof(err_msg),
                  "ERROR: Job #%lu: Too many CAN actions in step while staging %s.",
                  (unsigned long)job->job_id,
                  action_label);
         Dispatcher_SendUsbResponse(err_msg);
         JobManager_CompleteJob(job, JOB_STATUS_ERROR);
         return false;
     }

     staged_msgs[*staged_count] = *msg;
     (*staged_count)++;
     return true;
 }

 /*
  * Находит running job и transaction, соответствующие routed CAN response.
  * Это финальная защита JobManager от late/service/foreign responses.
  */
 static JobContext_t* JobManager_FindJobForRoutedResponse(
         const CanRoutedResponse_t* routed,
         ExecutorTransaction_t** out_tx,
		 bool require_channel)
 {
     if (out_tx != NULL) {
         *out_tx = NULL;
     }

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

         ExecutorTransaction_t* tx = JobManager_FindMatchingTransaction(
				 job,
				 routed,
				 require_channel);
         if (tx != NULL) {
             if (out_tx != NULL) {
                 *out_tx = tx;
             }
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

	const CAN_Response_t* response = &routed->parsed;
	bool matched = false;

	for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
		JobContext_t* job = &g_active_jobs[i];

		if (job->status != JOB_STATUS_RUNNING) {
			continue;
		}

		if (routed->context_job_id != 0U &&
				job->job_id != routed->context_job_id) {
			continue;
		}

		for (uint8_t tx_idx = 0; tx_idx < JOB_MAX_EXECUTOR_TRANSACTIONS; tx_idx++) {
			ExecutorTransaction_t* tx = &job->transactions[tx_idx];

			if (!tx->active ||
					JobManager_IsTerminalTransactionState(tx->state) ||
					tx->expected_node_id != response->source_addr ||
					tx->low_command_code != routed->context_command_code) {
				continue;
			}

			matched = true;

			if (tx->state == EXEC_TX_SENT) {
				tx->state = EXEC_TX_ACKED;
			}
		}
	}

	if (!matched) {
		JobManager_LogUnexpectedRoutedResponse(routed, "no matching transaction for ACK");
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

	const CAN_Response_t* response = &routed->parsed;
	bool matched = false;

	for (int i = 0; i < MAX_CONCURRENT_JOBS; i++) {
		JobContext_t* job = &g_active_jobs[i];
		bool job_matched = false;

		if (job->status != JOB_STATUS_RUNNING) {
			continue;
		}

		if (routed->context_job_id != 0U &&
				job->job_id != routed->context_job_id) {
			continue;
		}

		for (uint8_t tx_idx = 0; tx_idx < JOB_MAX_EXECUTOR_TRANSACTIONS; tx_idx++) {
			ExecutorTransaction_t* tx = &job->transactions[tx_idx];

			if (!tx->active ||
					JobManager_IsTerminalTransactionState(tx->state) ||
					tx->expected_node_id != response->source_addr ||
					tx->low_command_code != routed->context_command_code) {
				continue;
			}

			tx->state = EXEC_TX_NACK;
			tx->last_error_code = response->error_code;
			tx->active = false;
			matched = true;
			job_matched = true;
		}

		if (job_matched) {
			char msg[APP_USB_RESP_MAX_LEN];
			snprintf(msg, sizeof(msg),
					"ERROR: Executor NACK matched: job=%lu node=0x%02X cmd=0x%04X err=0x%04X.",
					(unsigned long)job->job_id,
					response->source_addr,
					routed->context_command_code,
					response->error_code);
			Dispatcher_SendUsbResponse(msg);

			JobManager_CompleteJob(job, JOB_STATUS_ERROR);
		}
	}

	if (!matched) {
		JobManager_LogUnexpectedRoutedResponse(routed, "no matching transaction for NACK");
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
	ExecutorTransaction_t* tx = NULL;
	JobContext_t* job = JobManager_FindJobForRoutedResponse(
			routed,
			&tx,
			routed != NULL && routed->context_channel_valid);

	if (job == NULL || tx == NULL) {
		JobManager_LogUnexpectedRoutedResponse(routed, "no matching transaction for DATA");
		return;
	}

	if (tx->state == EXEC_TX_SENT) {
		tx->state = EXEC_TX_ACKED;
	}

	if (tx->state != EXEC_TX_ACKED && tx->state != EXEC_TX_DATA_SEEN) {
		JobManager_LogUnexpectedRoutedResponse(routed, "DATA in invalid transaction state");
		JobManager_CompleteJob(job, JOB_STATUS_ERROR);
		return;
	}

	if (tx->response_policy == JOB_RESPONSE_DONE_ONLY) {
		JobManager_LogUnexpectedRoutedResponse(routed, "DATA for DONE_ONLY transaction");
		JobManager_CompleteJob(job, JOB_STATUS_ERROR);
		return;
	}

	if (tx->response_policy == JOB_RESPONSE_DATA_THEN_DONE && tx->data_count > 0U) {
		JobManager_LogUnexpectedRoutedResponse(routed, "extra DATA for DATA_THEN_DONE transaction");
		JobManager_CompleteJob(job, JOB_STATUS_ERROR);
		return;
	}

	tx->data_count++;
	tx->state = EXEC_TX_DATA_SEEN;
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
	ExecutorTransaction_t* tx = NULL;
	JobContext_t* job = JobManager_FindJobForRoutedResponse(routed, &tx, true);

	if (job == NULL || tx == NULL) {
		JobManager_LogUnexpectedRoutedResponse(routed, "no matching transaction for DONE");
		return;
	}

	if (tx->state == EXEC_TX_SENT) {
		tx->state = EXEC_TX_ACKED;
	}

	if (tx->state != EXEC_TX_ACKED && tx->state != EXEC_TX_DATA_SEEN) {
		JobManager_LogUnexpectedRoutedResponse(routed, "DONE in invalid transaction state");
		JobManager_CompleteJob(job, JOB_STATUS_ERROR);
		return;
	}

	if ((tx->response_policy == JOB_RESPONSE_DATA_THEN_DONE ||
			tx->response_policy == JOB_RESPONSE_MULTI_DATA_THEN_DONE) &&
			tx->data_count == 0U) {
		JobManager_LogUnexpectedRoutedResponse(routed, "DONE before required DATA");
		JobManager_CompleteJob(job, JOB_STATUS_ERROR);
		return;
	}

	tx->state = EXEC_TX_DONE;
	tx->active = false;

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

     for (uint8_t i = 0; i < JOB_MAX_EXECUTOR_TRANSACTIONS; i++) {
         ExecutorTransaction_t* tx = &job->transactions[i];

         if (!tx->active) {
             continue;
         }

         if (tx->state == EXEC_TX_SENT &&
             JobManager_TickElapsed(tx->sent_time_ms, tx->ack_timeout_ms)) {
             tx->state = EXEC_TX_ACK_TIMEOUT;

             char msg[APP_USB_RESP_MAX_LEN];
             snprintf(msg, sizeof(msg),
                      "ERROR: Job #%lu ACK timeout: node=0x%02X cmd=0x%04X.",
                      (unsigned long)job->job_id,
                      tx->expected_node_id,
                      tx->low_command_code);
             Dispatcher_SendUsbResponse(msg);

             JobManager_CompleteJob(job, JOB_STATUS_TIMEOUT);
             return true;
         }

         if ((tx->state == EXEC_TX_ACKED || tx->state == EXEC_TX_DATA_SEEN) &&
             JobManager_TickElapsed(tx->sent_time_ms, tx->operation_timeout_ms)) {
             tx->state = EXEC_TX_OPERATION_TIMEOUT;

             char msg[APP_USB_RESP_MAX_LEN];
             snprintf(msg, sizeof(msg),
                      "ERROR: Job #%lu operation timeout: node=0x%02X cmd=0x%04X.",
                      (unsigned long)job->job_id,
                      tx->expected_node_id,
                      tx->low_command_code);
             Dispatcher_SendUsbResponse(msg);

             JobManager_CompleteJob(job, JOB_STATUS_TIMEOUT);
             return true;
         }
     }

     return false;
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
			JobManager_CompleteJob(job, JOB_STATUS_ERROR);
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
        g_active_jobs[i].kind = JOB_KIND_HOST_RECIPE;
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
    job->kind = JOB_KIND_HOST_RECIPE;
    JobManager_ResetTransactions(job);
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

/*
 * Завершает один ожидаемый action внутри текущего recipe step.
 * Сейчас это legacy-точка продвижения pending_actions_count; в целевой схеме
 * она должна вызываться только после строгой проверки ExecutorTransaction.
 */
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
/*
 * Возвращает uint8_t-параметр atomic action.
 * Источник параметра может быть статическим значением из recipe или полем,
 * разобранным из исходной Host-команды.
 */
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

/*
 * Возвращает uint16_t-параметр atomic action.
 * Используется там, где recipe хранит default, но конкретная Host-команда
 * может переопределить значение через ParamSource_t.
 */
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

/*
 * Возвращает int32_t-параметр atomic action.
 * Здесь сосредоточены технологические пересчеты Host-полей в шаги
 * для Motion-исполнителя.
 */
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

/*
 * Возвращает uint32_t-параметр atomic action.
 * Используется для длительностей и других физических величин; при необходимости
 * вызывает calibrator/translator, а не хранит расчеты в parser-е.
 */
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
             JobManager_CompleteJob(job, JOB_STATUS_TIMEOUT);
             continue;
         }
      }
}


// --- Внутренние функции ---

/*
 * Ищет активный job по его runtime job_id.
 * Возвращает NULL, если job уже завершен, свободен или id не найден.
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
        JobManager_CompleteJob(job, JOB_STATUS_COMPLETED);
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

    CAN_Message_t staged_can_msgs[JOB_MAX_EXECUTOR_TRANSACTIONS];
    uint8_t staged_can_msg_count = 0U;

    for (int i = 0; i < current_step->num_actions; i++) {
        const AtomicAction_t* action = &current_step->atomic_actions[i];
        CAN_Message_t can_msg;
        DevicePhysAddr_t phys_addr;

        switch (action->action) {
            case ACTION_ROTATE_MOTOR: {
                uint8_t sys_id = JobManager_GetUint8Param(
                        job,
                        action->params.rotate_motor.motor_id_source,
                        action->params.rotate_motor.motor_id);
                int32_t steps = JobManager_GetInt32Param(
                        job,
                        action->params.rotate_motor.steps_source,
                        action->params.rotate_motor.steps);
                uint16_t speed = JobManager_GetUint16Param(
                        job,
                        action->params.rotate_motor.speed_source,
                        action->params.rotate_motor.speed);

                uint32_t abs_steps = JobManager_AbsSteps(steps);
                if (abs_steps != 0U && speed == 0U) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: Invalid Motion speed 0 for SysID %u",
                            (unsigned long)job->job_id,
                            sys_id);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }

                JobManager_ExtendStepTimeout(
                        job,
                        JobManager_CalcMotionRotateTimeoutMs(steps, speed));

                phys_addr = DeviceMapping_GetMotorPhysAddr(sys_id);
                if (!phys_addr.is_valid) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: Invalid Motor SysID %u",
                            (unsigned long)job->job_id,
                            sys_id);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }

                if (JobManager_RegisterExecutorTransaction(
                        job,
                        CAN_CMD_MOTOR_ROTATE,
                        phys_addr.node_id,
                        phys_addr.ch_idx,
                        true,
                        JOB_RESPONSE_DONE_ONLY,
                        job->step_timeout_ms) == NULL) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: Cannot register ROTATE_MOTOR transaction (Node:0x%02X, Ch:%u)",
                            (unsigned long)job->job_id,
                            phys_addr.node_id,
                            phys_addr.ch_idx);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }

                snprintf(info_msg, sizeof(info_msg),
                        "DEBUG: Job #%lu: Queued ROTATE_MOTOR (Phys:%u:%u, Steps:%ld, Speed:%u)",
                        (unsigned long)job->job_id,
                        phys_addr.node_id,
                        phys_addr.ch_idx,
                        (long)steps,
                        speed);
                Dispatcher_SendUsbResponse(info_msg);

                Packer_CreateRotateMotorMsg(phys_addr.ch_idx, steps, speed, &can_msg);
                can_msg.id = CAN_BUILD_ID(
                        CAN_PRIORITY_HIGH,
                        CAN_MSG_TYPE_COMMAND,
                        phys_addr.node_id,
                        CAN_ADDR_CONDUCTOR);
                if (!JobManager_StageCanTx(
                        job,
                        staged_can_msgs,
                        &staged_can_msg_count,
                        &can_msg,
                        "ROTATE_MOTOR")) {
                    return;
                }
                break;
            }

            case ACTION_HOME_MOTOR: {
                uint8_t sys_id = JobManager_GetUint8Param(
                        job,
                        action->params.home_motor.motor_id_source,
                        action->params.home_motor.motor_id);
                uint16_t speed = JobManager_GetUint16Param(
                        job,
                        action->params.home_motor.speed_source,
                        action->params.home_motor.speed);

                // Фильтрация по маске для инициализации.
                if (job->initial_recipe_id == RECIPE_INITIALIZE_SYSTEM &&
                        job->initial_cmd.args_type == ARGS_TYPE_PARSED) {
                    uint8_t modules_mask = job->initial_cmd.args.init.modules_mask;
                    if ((modules_mask & (1U << (sys_id - 1U))) == 0U) {
                        job->pending_actions_count--;
                        continue;
                    }
                }

                if (speed == 0U) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: Invalid HOME speed 0 for SysID %u",
                            (unsigned long)job->job_id,
                            sys_id);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }

                JobManager_ExtendStepTimeout(job, JOB_MOTION_HOME_TIMEOUT_MS);

                phys_addr = DeviceMapping_GetMotorPhysAddr(sys_id);
                if (!phys_addr.is_valid) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: Invalid Motor SysID %u",
                            (unsigned long)job->job_id,
                            sys_id);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }

                if (JobManager_RegisterExecutorTransaction(
                        job,
                        CAN_CMD_MOTOR_HOME,
                        phys_addr.node_id,
                        phys_addr.ch_idx,
                        true,
                        JOB_RESPONSE_DONE_ONLY,
                        job->step_timeout_ms) == NULL) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: Cannot register HOME_MOTOR transaction (Node:0x%02X, Ch:%u)",
                            (unsigned long)job->job_id,
                            phys_addr.node_id,
                            phys_addr.ch_idx);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }

                snprintf(info_msg, sizeof(info_msg),
                        "DEBUG: Job #%lu: Queued HOME_MOTOR (Phys:%u:%u, Speed:%u)",
                        (unsigned long)job->job_id,
                        phys_addr.node_id,
                        phys_addr.ch_idx,
                        speed);
                Dispatcher_SendUsbResponse(info_msg);

                Packer_CreateHomeMotorMsg(phys_addr.ch_idx, speed, &can_msg);
                can_msg.id = CAN_BUILD_ID(
                        CAN_PRIORITY_HIGH,
                        CAN_MSG_TYPE_COMMAND,
                        phys_addr.node_id,
                        CAN_ADDR_CONDUCTOR);
                if (!JobManager_StageCanTx(
                        job,
                        staged_can_msgs,
                        &staged_can_msg_count,
                        &can_msg,
                        "HOME_MOTOR")) {
                    return;
                }
                break;
            }

            case ACTION_RUN_PUMP_DURATION: {
                uint8_t sys_id = JobManager_GetUint8Param(
                        job,
                        action->params.pump_duration.pump_id_source,
                        action->params.pump_duration.pump_id);
                uint32_t duration_ms = JobManager_GetUint32Param(
                        job,
                        action->params.pump_duration.duration_ms_source,
                        action->params.pump_duration.duration_ms);

                if (duration_ms == 0U) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: Invalid pump duration for SysID %u",
                            (unsigned long)job->job_id,
                            sys_id);
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
                            (unsigned long)job->job_id,
                            sys_id);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }

                if (JobManager_RegisterExecutorTransaction(
                        job,
                        CAN_CMD_PUMP_RUN_DURATION,
                        phys_addr.node_id,
                        phys_addr.ch_idx,
                        true,
                        JOB_RESPONSE_DONE_ONLY,
                        job->step_timeout_ms) == NULL) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: Cannot register RUN_PUMP_DURATION transaction (Node:0x%02X, Ch:%u)",
                            (unsigned long)job->job_id,
                            phys_addr.node_id,
                            phys_addr.ch_idx);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }

                snprintf(info_msg, sizeof(info_msg),
                        "DEBUG: Job #%lu: Queued RUN_PUMP_DURATION (Phys:%u:%u, Duration:%lu)",
                        (unsigned long)job->job_id,
                        phys_addr.node_id,
                        phys_addr.ch_idx,
                        (unsigned long)duration_ms);
                Dispatcher_SendUsbResponse(info_msg);

                Packer_CreatePumpRunDurationMsg(phys_addr.ch_idx, duration_ms, &can_msg);
                can_msg.id = CAN_BUILD_ID(
                        CAN_PRIORITY_HIGH,
                        CAN_MSG_TYPE_COMMAND,
                        phys_addr.node_id,
                        CAN_ADDR_CONDUCTOR);
                if (!JobManager_StageCanTx(
                        job,
                        staged_can_msgs,
                        &staged_can_msg_count,
                        &can_msg,
                        "RUN_PUMP_DURATION")) {
                    return;
                }
                break;
            }

            case ACTION_START_PUMP: {
                uint8_t sys_id = JobManager_GetUint8Param(
                        job,
                        action->params.pump.pump_id_source,
                        action->params.pump.pump_id);

                phys_addr = DeviceMapping_GetFluidicPhysAddr(sys_id);
                if (!phys_addr.is_valid) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: Invalid Pump SysID %u",
                            (unsigned long)job->job_id,
                            sys_id);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }

                if (JobManager_RegisterExecutorTransaction(
                        job,
                        CAN_CMD_PUMP_START,
                        phys_addr.node_id,
                        phys_addr.ch_idx,
                        true,
                        JOB_RESPONSE_DONE_ONLY,
                        job->step_timeout_ms) == NULL) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: Cannot register START_PUMP transaction (Node:0x%02X, Ch:%u)",
                            (unsigned long)job->job_id,
                            phys_addr.node_id,
                            phys_addr.ch_idx);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }

                snprintf(info_msg, sizeof(info_msg),
                        "DEBUG: Job #%lu: Queued START_PUMP (Phys:%u:%u)",
                        (unsigned long)job->job_id,
                        phys_addr.node_id,
                        phys_addr.ch_idx);
                Dispatcher_SendUsbResponse(info_msg);

                Packer_CreatePumpStartMsg(phys_addr.ch_idx, 0, &can_msg);
                can_msg.id = CAN_BUILD_ID(
                        CAN_PRIORITY_HIGH,
                        CAN_MSG_TYPE_COMMAND,
                        phys_addr.node_id,
                        CAN_ADDR_CONDUCTOR);
                if (!JobManager_StageCanTx(
                        job,
                        staged_can_msgs,
                        &staged_can_msg_count,
                        &can_msg,
                        "START_PUMP")) {
                    return;
                }
                break;
            }

            case ACTION_STOP_PUMP: {
                uint8_t sys_id = JobManager_GetUint8Param(
                        job,
                        action->params.pump.pump_id_source,
                        action->params.pump.pump_id);

                phys_addr = DeviceMapping_GetFluidicPhysAddr(sys_id);
                if (!phys_addr.is_valid) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: Invalid Pump SysID %u",
                            (unsigned long)job->job_id,
                            sys_id);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }

                if (JobManager_RegisterExecutorTransaction(
                        job,
                        CAN_CMD_PUMP_STOP,
                        phys_addr.node_id,
                        phys_addr.ch_idx,
                        true,
                        JOB_RESPONSE_DONE_ONLY,
                        job->step_timeout_ms) == NULL) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: Cannot register STOP_PUMP transaction (Node:0x%02X, Ch:%u)",
                            (unsigned long)job->job_id,
                            phys_addr.node_id,
                            phys_addr.ch_idx);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                    return;
                }

                snprintf(info_msg, sizeof(info_msg),
                        "DEBUG: Job #%lu: Queued STOP_PUMP (Phys:%u:%u)",
                        (unsigned long)job->job_id,
                        phys_addr.node_id,
                        phys_addr.ch_idx);
                Dispatcher_SendUsbResponse(info_msg);

                Packer_CreatePumpStopMsg(phys_addr.ch_idx, &can_msg);
                can_msg.id = CAN_BUILD_ID(
                        CAN_PRIORITY_HIGH,
                        CAN_MSG_TYPE_COMMAND,
                        phys_addr.node_id,
                        CAN_ADDR_CONDUCTOR);
                if (!JobManager_StageCanTx(
                        job,
                        staged_can_msgs,
                        &staged_can_msg_count,
                        &can_msg,
                        "STOP_PUMP")) {
                    return;
                }
                break;
            }

            case ACTION_WAIT_MS: {
                uint32_t delay_ms = JobManager_GetUint32Param(
                        job,
                        action->params.wait.delay_ms_source,
                        action->params.wait.delay_ms);

                if (JobManager_RegisterInternalAction(
                        job,
                        JOB_INTERNAL_ACTION_WAIT_MS,
                        delay_ms) == NULL) {
                    snprintf(info_msg, sizeof(info_msg),
                            "ERROR: Job #%lu: Cannot register WAIT_MS internal action (%lu ms).",
                            (unsigned long)job->job_id,
                            (unsigned long)delay_ms);
                    Dispatcher_SendUsbResponse(info_msg);
                    JobManager_CompleteJob(job, JOB_STATUS_ERROR);
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
                uint8_t sys_id = JobManager_GetUint8Param(
                        job,
                        action->params.perform_scan.photometer_id_source,
                        action->params.perform_scan.photometer_id);
                uint8_t mask = JobManager_GetUint8Param(
                        job,
                        action->params.perform_scan.wavelength_mask_source,
                        action->params.perform_scan.wavelength_mask);

                snprintf(info_msg, sizeof(info_msg),
                        "ERROR: Job #%lu: ACTION_PERFORM_SCAN unsupported "
                        "(Photometer SysID:%u, WavelengthMask:0x%02X).",
                        (unsigned long)job->job_id,
                        sys_id,
                        mask);
                Dispatcher_SendUsbResponse(info_msg);

                JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                return;
            }

            default:
                snprintf(info_msg, sizeof(info_msg),
                        "ERROR: Job #%lu: Unknown action %d",
                        (unsigned long)job->job_id,
                        (int)action->action);
                Dispatcher_SendUsbResponse(info_msg);
                JobManager_CompleteJob(job, JOB_STATUS_ERROR);
                return;
        }
    }

    if (staged_can_msg_count > 0U &&
            uxQueueSpacesAvailable(can_tx_queue_handle) < staged_can_msg_count) {
        snprintf(info_msg, sizeof(info_msg),
                "ERROR: Job #%lu: CAN TX queue has no room for %u staged actions.",
                (unsigned long)job->job_id,
                (unsigned int)staged_can_msg_count);
        Dispatcher_SendUsbResponse(info_msg);
        JobManager_CompleteJob(job, JOB_STATUS_ERROR);
        return;
    }

    for (uint8_t i = 0; i < staged_can_msg_count; i++) {
        if (xQueueSend(can_tx_queue_handle, &staged_can_msgs[i], 0) != pdPASS) {
            snprintf(info_msg, sizeof(info_msg),
                    "ERROR: Job #%lu: Failed to enqueue staged CAN action %u/%u.",
                    (unsigned long)job->job_id,
                    (unsigned int)(i + 1U),
                    (unsigned int)staged_can_msg_count);
            Dispatcher_SendUsbResponse(info_msg);
            JobManager_CompleteJob(job, JOB_STATUS_ERROR);
            return;
        }
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
    JobManager_ResetTransactions(job);
    job->kind = JOB_KIND_HOST_RECIPE;

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
