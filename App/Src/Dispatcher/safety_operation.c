/*
 * safety_operation.c
 *
 * Confirmed safety lifecycle for Host 0x1010 EMERGENCY_STOP.
 *
 * Owner boundary:
 * - вызов: direct_command_handlers для Host `0x1010`;
 * - polling: task_jobs_monitor вызывает SafetyOperation_Run() первым после router;
 * - TX: confirmed safe-state waves через executor_command_tx;
 * - RX: только can_safety_rx_queue_handle через CanResponseRouter;
 * - effect: abort active Host job, отправить safe-state commands, latch ERROR.
 *
 * Этот модуль не использует raw broadcast stop и не выполняет service/recovery.
 * Выход из latched emergency state выполняется только успешным INIT path.
 */

#include "Dispatcher/safety_operation.h"
#include "Dispatcher/can_packer.h"
#include "Dispatcher/can_response_router.h"
#include "Dispatcher/dispatcher_io.h"
#include "Dispatcher/executor_command_tx.h"
#include "Dispatcher/executor_transaction.h"
#include "Dispatcher/job_manager.h"
#include "Dispatcher/service_manager.h"
#include "shared_resources.h"
#include "task_dispatcher.h"
#include "app_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define SAFETY_OPERATION_FIRST_ID              1U
#define SAFETY_MOTION_MAX_CHANNELS             8U
#define SAFETY_FLUIDICS_PUMP_CHANNELS          13U
#define SAFETY_FLUIDICS_TOTAL_CHANNELS         16U
#define SAFETY_MAX_COMMANDS                    (SAFETY_MOTION_MAX_CHANNELS + SAFETY_FLUIDICS_TOTAL_CHANNELS)
#define SAFETY_EXECUTOR_OPERATION_TIMEOUT_MS   1000U

typedef enum {
	SAFETY_STATE_IDLE = 0,
	SAFETY_STATE_START_REQUESTED,
	SAFETY_STATE_RUNNING,
	SAFETY_STATE_LATCHED
} SafetyOperationState_t;

typedef enum {
	SAFETY_COMMAND_MOTION_STOP = 0,
	SAFETY_COMMAND_PUMP_STOP,
	SAFETY_COMMAND_VALVE_CLOSE
} SafetyCommandType_t;

typedef struct {
	uint8_t node_id;
	uint8_t channel;
	uint16_t command_code;
	SafetyCommandType_t type;
} SafetyPlanItem_t;

static SafetyOperationState_t g_safety_state = SAFETY_STATE_IDLE;
static ExecutorTransactionTable_t g_safety_transactions;
static SafetyPlanItem_t g_safety_plan[SAFETY_MAX_COMMANDS];
static uint8_t g_safety_plan_count = 0;
static uint8_t g_safety_next_plan_index = 0;
static uint8_t g_safety_active_wave_count = 0;
static uint32_t g_safety_operation_id = SAFETY_OPERATION_FIRST_ID;
static uint32_t g_safety_next_operation_id = SAFETY_OPERATION_FIRST_ID;
static uint16_t g_safety_host_command_code = 0U;
static uint16_t g_safety_last_result = HOST_STATUS_OK;
static bool g_safety_latch_confirmed = false;

static uint32_t SafetyOperation_NextOperationId(void)
{
	uint32_t operation_id = g_safety_next_operation_id++;
	if (g_safety_next_operation_id == 0U) {
		g_safety_next_operation_id = SAFETY_OPERATION_FIRST_ID;
	}
	return operation_id;
}

static void SafetyOperation_DrainPendingResponses(void)
{
	if (can_safety_rx_queue_handle == NULL) {
		return;
	}

	CanRoutedResponse_t routed;
	while (xQueueReceive(can_safety_rx_queue_handle, &routed, 0) == pdPASS) {
	}
}

static void SafetyOperation_ClearPlan(void)
{
	memset(g_safety_plan, 0, sizeof(g_safety_plan));
	g_safety_plan_count = 0;
	g_safety_next_plan_index = 0;
	g_safety_active_wave_count = 0;
}

static bool SafetyOperation_AddPlanItem(uint8_t node_id,
                                        uint8_t channel,
                                        uint16_t command_code,
                                        SafetyCommandType_t type)
{
	if (g_safety_plan_count >= SAFETY_MAX_COMMANDS) {
		return false;
	}

	SafetyPlanItem_t* item = &g_safety_plan[g_safety_plan_count++];
	item->node_id = node_id;
	item->channel = channel;
	item->command_code = command_code;
	item->type = type;
	return true;
}

static bool SafetyOperation_BuildPlan(void)
{
	SafetyOperation_ClearPlan();

	uint8_t channel_count = 0;
	if (ServiceManager_GetNodeChannelCount(CAN_ADDR_MOTOR_BOARD, &channel_count)) {
		if (channel_count > SAFETY_MOTION_MAX_CHANNELS) {
			channel_count = SAFETY_MOTION_MAX_CHANNELS;
		}

		for (uint8_t channel = 0; channel < channel_count; channel++) {
			if (!SafetyOperation_AddPlanItem(CAN_ADDR_MOTOR_BOARD,
			                                 channel,
			                                 CAN_CMD_MOTOR_STOP,
			                                 SAFETY_COMMAND_MOTION_STOP)) {
				return false;
			}
		}
	}

	channel_count = 0;
	if (ServiceManager_GetNodeChannelCount(CAN_ADDR_PUMP_BOARD, &channel_count)) {
		if (channel_count > SAFETY_FLUIDICS_TOTAL_CHANNELS) {
			channel_count = SAFETY_FLUIDICS_TOTAL_CHANNELS;
		}

		uint8_t pump_limit = channel_count;
		if (pump_limit > SAFETY_FLUIDICS_PUMP_CHANNELS) {
			pump_limit = SAFETY_FLUIDICS_PUMP_CHANNELS;
		}

		for (uint8_t channel = 0; channel < pump_limit; channel++) {
			if (!SafetyOperation_AddPlanItem(CAN_ADDR_PUMP_BOARD,
			                                 channel,
			                                 CAN_CMD_PUMP_STOP,
			                                 SAFETY_COMMAND_PUMP_STOP)) {
				return false;
			}
		}

		for (uint8_t channel = SAFETY_FLUIDICS_PUMP_CHANNELS;
				channel < channel_count;
				channel++) {
			if (!SafetyOperation_AddPlanItem(CAN_ADDR_PUMP_BOARD,
			                                 channel,
			                                 CAN_CMD_VALVE_CLOSE,
			                                 SAFETY_COMMAND_VALVE_CLOSE)) {
				return false;
			}
		}
	}

	return true;
}

static uint16_t SafetyOperation_MapTxStatusToHostError(ExecutorCommandTxStatus_t status)
{
	switch (status) {
		case EXECUTOR_COMMAND_TX_DUPLICATE_TRANSACTION:
		case EXECUTOR_COMMAND_TX_QUEUE_FULL:
			return HOST_ERR_BUSY;

		case EXECUTOR_COMMAND_TX_QUEUE_SEND_FAILED:
		case EXECUTOR_COMMAND_TX_ROUTE_REGISTRATION_FAILED:
			return HOST_ERR_HARDWARE;

		case EXECUTOR_COMMAND_TX_INVALID_ARG:
		case EXECUTOR_COMMAND_TX_INVALID_FRAME:
		default:
			return HOST_ERR_GENERAL;
	}
}

static uint16_t SafetyOperation_MapNackToHostError(const ExecutorTransactionUpdate_t* update)
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
		case CAN_NACK_ERR_THERMO_BUSY:
			return HOST_ERR_BUSY;

		case CAN_NACK_ERR_FLASH_WRITE:
			return HOST_ERR_HARDWARE;

		default:
			return HOST_ERR_GENERAL;
	}
}

static void SafetyOperation_SetFrameDestination(CAN_Message_t* frame, uint8_t node_id)
{
	if (frame == NULL) {
		return;
	}

	frame->id = CAN_BUILD_ID(CAN_PRIORITY_HIGH,
	                         CAN_MSG_TYPE_COMMAND,
	                         node_id,
	                         CAN_ADDR_CONDUCTOR);
}

static void SafetyOperation_BuildExecutorCommand(const SafetyPlanItem_t* item,
                                                 ExecutorCommandTxCommand_t* command)
{
	memset(command, 0, sizeof(*command));

	switch (item->type) {
		case SAFETY_COMMAND_MOTION_STOP:
			Packer_CreateStopMotorMsg(item->channel, &command->frame);
			break;

		case SAFETY_COMMAND_PUMP_STOP:
			Packer_CreatePumpStopMsg(item->channel, &command->frame);
			break;

		case SAFETY_COMMAND_VALVE_CLOSE:
		default:
			Packer_CreateValveCloseMsg(item->channel, &command->frame);
			break;
	}

	SafetyOperation_SetFrameDestination(&command->frame, item->node_id);

	command->transaction.route_owner = TX_OWNER_SAFETY_OPERATION;
	command->transaction.operation_id = g_safety_operation_id;
	command->transaction.host_command_code = g_safety_host_command_code;
	command->transaction.low_command_code = item->command_code;
	command->transaction.expected_node_id = item->node_id;
	command->transaction.expected_channel = item->channel;
	command->transaction.channel_valid = true;
	command->transaction.response_policy = EXECUTOR_TRANSACTION_RESPONSE_DONE_ONLY;
	command->transaction.operation_timeout_ms = SAFETY_EXECUTOR_OPERATION_TIMEOUT_MS;
}

static void SafetyOperation_Finish(uint16_t host_status_code)
{
	uint16_t final_status = host_status_code;
	if (final_status == HOST_ERR_OK) {
		final_status = HOST_STATUS_OK;
	}

	ExecutorTransactionTable_ResetOperation(
			&g_safety_transactions,
			TX_OWNER_SAFETY_OPERATION,
			g_safety_operation_id);
	SafetyOperation_DrainPendingResponses();
	SafetyOperation_ClearPlan();

	if (final_status == HOST_STATUS_OK) {
		SetSystemError(HOST_ERR_EMERGENCY_STOP);
	}
	else {
		SetSystemError(final_status);
	}

	uint16_t host_command_code = g_safety_host_command_code;

	taskENTER_CRITICAL();
	g_safety_last_result = final_status;
	g_safety_latch_confirmed = (final_status == HOST_STATUS_OK);
	g_safety_state = SAFETY_STATE_LATCHED;
	taskEXIT_CRITICAL();

	if (host_command_code != 0U) {
		Dispatcher_SendDone(host_command_code, final_status);
	}
}

static bool SafetyOperation_SubmitNextWave(void)
{
	ExecutorCommandTxCommand_t commands[JOB_MAX_EXECUTOR_TRANSACTIONS];
	uint8_t command_count = 0;

	while (g_safety_next_plan_index < g_safety_plan_count &&
			command_count < JOB_MAX_EXECUTOR_TRANSACTIONS) {
		const SafetyPlanItem_t* item = &g_safety_plan[g_safety_next_plan_index++];
		SafetyOperation_BuildExecutorCommand(item, &commands[command_count]);
		command_count++;
	}

	if (command_count == 0U) {
		return true;
	}

	ExecutorCommandTxStatus_t tx_status =
			ExecutorCommandTx_SubmitBatch(&g_safety_transactions,
			                              commands,
			                              command_count);
	if (tx_status != EXECUTOR_COMMAND_TX_OK) {
		SafetyOperation_Finish(SafetyOperation_MapTxStatusToHostError(tx_status));
		return false;
	}

	g_safety_active_wave_count = command_count;
	return true;
}

static void SafetyOperation_Begin(void)
{
	taskENTER_CRITICAL();
	if (g_safety_state != SAFETY_STATE_START_REQUESTED) {
		taskEXIT_CRITICAL();
		return;
	}
	g_safety_state = SAFETY_STATE_RUNNING;
	taskEXIT_CRITICAL();

	JobManager_AbortAll(HOST_ERR_EMERGENCY_STOP);
	SetSystemError(HOST_ERR_EMERGENCY_STOP);
	SafetyOperation_DrainPendingResponses();
	ExecutorTransactionTable_Reset(&g_safety_transactions);

	if (!SafetyOperation_BuildPlan()) {
		SafetyOperation_Finish(HOST_ERR_GENERAL);
		return;
	}

	if (g_safety_plan_count == 0U) {
		SafetyOperation_Finish(HOST_ERR_NOT_INIT);
		return;
	}

	(void)SafetyOperation_SubmitNextWave();
}

static void SafetyOperation_HandleTransactionUpdate(
		const ExecutorTransactionUpdate_t* update)
{
	if (update == NULL || !update->matched) {
		return;
	}

	switch (update->event) {
		case EXECUTOR_TRANSACTION_EVENT_DONE:
			if (g_safety_active_wave_count > 0U) {
				g_safety_active_wave_count--;
			}
			break;

		case EXECUTOR_TRANSACTION_EVENT_NACK:
			SafetyOperation_Finish(SafetyOperation_MapNackToHostError(update));
			break;

		case EXECUTOR_TRANSACTION_EVENT_PROTOCOL_ERROR:
			SafetyOperation_Finish(HOST_ERR_GENERAL);
			break;

		case EXECUTOR_TRANSACTION_EVENT_ACK_TIMEOUT:
		case EXECUTOR_TRANSACTION_EVENT_OPERATION_TIMEOUT:
			SafetyOperation_Finish(HOST_ERR_TIMEOUT);
			break;

		case EXECUTOR_TRANSACTION_EVENT_ACKED:
		case EXECUTOR_TRANSACTION_EVENT_DATA_ACCEPTED:
		case EXECUTOR_TRANSACTION_EVENT_NONE:
		default:
			break;
	}
}

static void SafetyOperation_ProcessRoutedResponse(const CanRoutedResponse_t* routed)
{
	if (routed == NULL ||
			!routed->context_valid ||
			routed->context_owner != TX_OWNER_SAFETY_OPERATION) {
		return;
	}

	ExecutorTransactionUpdate_t update;
	memset(&update, 0, sizeof(update));

	const CAN_Response_t* response = &routed->parsed;
	switch (response->msg_type) {
		case CAN_MSG_TYPE_ACK:
			update = ExecutorTransactionTable_HandleAck(
					&g_safety_transactions,
					routed);
			break;

		case CAN_MSG_TYPE_NACK:
			update = ExecutorTransactionTable_HandleNack(
					&g_safety_transactions,
					routed);
			break;

		case CAN_MSG_TYPE_DATA_DONE_LOG:
			if (response->sub_type == CAN_SUB_TYPE_DONE) {
				update = ExecutorTransactionTable_HandleDone(
						&g_safety_transactions,
						routed);
			}
			else if (response->sub_type == CAN_SUB_TYPE_DATA) {
				update = ExecutorTransactionTable_HandleData(
						&g_safety_transactions,
						routed);
			}
			else {
				return;
			}
			break;

		default:
			return;
	}

	SafetyOperation_HandleTransactionUpdate(&update);
}

static void SafetyOperation_DrainRoutedResponses(void)
{
	CanRoutedResponse_t routed;
	while (xQueueReceive(can_safety_rx_queue_handle, &routed, 0) == pdPASS) {
		SafetyOperation_ProcessRoutedResponse(&routed);
		if (g_safety_state != SAFETY_STATE_RUNNING) {
			break;
		}
	}
}

static void SafetyOperation_CheckTimeouts(void)
{
	ExecutorTransactionUpdate_t update;
	if (ExecutorTransactionTable_CheckTimeouts(&g_safety_transactions,
	                                           HAL_GetTick(),
	                                           &update)) {
		SafetyOperation_HandleTransactionUpdate(&update);
	}
}

void SafetyOperation_Init(void)
{
	taskENTER_CRITICAL();
	g_safety_state = SAFETY_STATE_IDLE;
	g_safety_operation_id = SAFETY_OPERATION_FIRST_ID;
	g_safety_next_operation_id = SAFETY_OPERATION_FIRST_ID;
	g_safety_host_command_code = 0U;
	g_safety_last_result = HOST_STATUS_OK;
	g_safety_latch_confirmed = false;
	taskEXIT_CRITICAL();

	ExecutorTransactionTable_Reset(&g_safety_transactions);
	SafetyOperation_ClearPlan();
	SafetyOperation_DrainPendingResponses();
}

void SafetyOperation_Start(uint16_t host_command_code)
{
	bool send_immediate_done = false;
	uint16_t immediate_status = HOST_STATUS_OK;
	bool accepted = false;

	taskENTER_CRITICAL();
	switch (g_safety_state) {
		case SAFETY_STATE_IDLE:
			g_safety_state = SAFETY_STATE_START_REQUESTED;
			g_safety_operation_id = SafetyOperation_NextOperationId();
			g_safety_host_command_code = host_command_code;
			g_safety_last_result = HOST_ERR_EMERGENCY_STOP;
			g_safety_latch_confirmed = false;
			accepted = true;
			break;

		case SAFETY_STATE_START_REQUESTED:
		case SAFETY_STATE_RUNNING:
			immediate_status = HOST_ERR_BUSY;
			send_immediate_done = true;
			break;

		case SAFETY_STATE_LATCHED:
		default:
			immediate_status = g_safety_latch_confirmed
					? HOST_STATUS_OK
					: g_safety_last_result;
			send_immediate_done = true;
			break;
	}
	taskEXIT_CRITICAL();

	if (accepted) {
		SetSystemError(HOST_ERR_EMERGENCY_STOP);
		Dispatcher_SendUsbResponse("WARNING: EMERGENCY_STOP safety operation requested.");
	}
	else if (send_immediate_done) {
		Dispatcher_SendDone(host_command_code, immediate_status);
	}
}

void SafetyOperation_Run(void)
{
	if (g_safety_state == SAFETY_STATE_START_REQUESTED) {
		SafetyOperation_Begin();
	}

	if (g_safety_state != SAFETY_STATE_RUNNING) {
		return;
	}

	if (can_safety_rx_queue_handle == NULL) {
		SafetyOperation_Finish(HOST_ERR_GENERAL);
		return;
	}

	SafetyOperation_DrainRoutedResponses();
	if (g_safety_state != SAFETY_STATE_RUNNING) {
		return;
	}

	SafetyOperation_CheckTimeouts();
	if (g_safety_state != SAFETY_STATE_RUNNING) {
		return;
	}

	if (g_safety_active_wave_count == 0U) {
		if (g_safety_next_plan_index >= g_safety_plan_count) {
			SafetyOperation_Finish(HOST_STATUS_OK);
		}
		else {
			(void)SafetyOperation_SubmitNextWave();
		}
	}
}

void SafetyOperation_ClearLatch(void)
{
	taskENTER_CRITICAL();
	if (g_safety_state == SAFETY_STATE_LATCHED) {
		g_safety_state = SAFETY_STATE_IDLE;
		g_safety_host_command_code = 0U;
		g_safety_last_result = HOST_STATUS_OK;
		g_safety_latch_confirmed = false;
	}
	taskEXIT_CRITICAL();
}

bool SafetyOperation_IsLatchedOrActive(void)
{
	bool latched_or_active;

	taskENTER_CRITICAL();
	latched_or_active = (g_safety_state != SAFETY_STATE_IDLE);
	taskEXIT_CRITICAL();

	return latched_or_active;
}

bool SafetyOperation_IsLatched(void)
{
	bool latched;

	taskENTER_CRITICAL();
	latched = (g_safety_state == SAFETY_STATE_LATCHED);
	taskEXIT_CRITICAL();

	return latched;
}
