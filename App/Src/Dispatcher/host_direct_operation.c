/*
 * host_direct_operation.c
 *
 * Executor-backed Host direct commands.
 *
 * Owner boundary:
 * - вызов: direct_command_handlers для Host `0x9010/0x9011`;
 * - polling: task_jobs_monitor вызывает HostDirectOperation_Run();
 * - TX: только через executor_command_tx, raw CAN здесь запрещен;
 * - RX: только can_host_direct_rx_queue_handle через CanResponseRouter;
 * - Host lifecycle: parser уже отправил ACK, этот модуль отправляет DATA/DONE
 *   или ERROR по исходному Host command code.
 *
 * Текущий scope: Temperature Sensors Executor `0x9010/0x9011`.
 * Legacy `0x8000 THERMO_GET_TEMP` здесь оставлен explicit unsupported до
 * отдельного thermostat policy.
 */

#include "Dispatcher/host_direct_operation.h"
#include "Dispatcher/can_packer.h"
#include "Dispatcher/can_response_router.h"
#include "Dispatcher/dispatcher_io.h"
#include "Dispatcher/executor_command_tx.h"
#include "Dispatcher/executor_transaction.h"
#include "Dispatcher/job_manager.h"
#include "Dispatcher/service_manager.h"
#include "Dispatcher/safety_operation.h"
#include "shared_resources.h"
#include "task_dispatcher.h"
#include "app_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define HOST_DIRECT_OPERATION_FIRST_ID          1U
#define HOST_DIRECT_THERMO_EXECUTOR_CHANNELS   8U
#define HOST_DIRECT_THERMO_MAX_SENSORS         HOST_DIRECT_THERMO_EXECUTOR_CHANNELS
#define HOST_DIRECT_THERMO_TIMEOUT_MS          1000U
#define HOST_DIRECT_TEMP_STATUS_NORMAL         0U

typedef enum {
	HOST_DIRECT_STATE_IDLE = 0,
	HOST_DIRECT_STATE_START_REQUESTED,
	HOST_DIRECT_STATE_RUNNING
} HostDirectOperationState_t;

typedef enum {
	HOST_DIRECT_KIND_NONE = 0,
	HOST_DIRECT_KIND_SENSOR_GET_ALL_TEMPS,
	HOST_DIRECT_KIND_SENSOR_GET_TEMP
} HostDirectOperationKind_t;

typedef struct {
	uint8_t sensor_id;
	int16_t temperature_deci_c;
	uint8_t status;
} HostDirectTempRecord_t;

static HostDirectOperationState_t g_host_direct_state = HOST_DIRECT_STATE_IDLE;
static HostDirectOperationKind_t g_host_direct_kind = HOST_DIRECT_KIND_NONE;
static ExecutorTransactionTable_t g_host_direct_transactions;
static uint32_t g_host_direct_operation_id = HOST_DIRECT_OPERATION_FIRST_ID;
static uint32_t g_host_direct_next_operation_id = HOST_DIRECT_OPERATION_FIRST_ID;
static uint16_t g_host_direct_host_command_code = 0U;
static uint8_t g_host_direct_sensor_id = 0U;
static uint8_t g_host_direct_expected_channel = 0U;
static bool g_host_direct_single_temp_valid = false;
static int16_t g_host_direct_single_temp_deci_c = 0;
static HostDirectTempRecord_t g_host_direct_temp_records[HOST_DIRECT_THERMO_MAX_SENSORS];
static uint8_t g_host_direct_temp_record_count = 0U;

static uint32_t HostDirectOperation_NextOperationId(void)
{
	uint32_t operation_id = g_host_direct_next_operation_id++;
	if (g_host_direct_next_operation_id == 0U) {
		g_host_direct_next_operation_id = HOST_DIRECT_OPERATION_FIRST_ID;
	}
	return operation_id;
}

static void HostDirectOperation_ClearData(void)
{
	g_host_direct_single_temp_valid = false;
	g_host_direct_single_temp_deci_c = 0;
	memset(g_host_direct_temp_records, 0, sizeof(g_host_direct_temp_records));
	g_host_direct_temp_record_count = 0U;
}

static void HostDirectOperation_DrainPendingResponses(void)
{
	if (can_host_direct_rx_queue_handle == NULL) {
		return;
	}

	CanRoutedResponse_t routed;
	while (xQueueReceive(can_host_direct_rx_queue_handle, &routed, 0) == pdPASS) {
	}
}

static int16_t HostDirectOperation_ReadI16Le(const uint8_t* src)
{
	uint16_t value = (uint16_t)(src[0] | ((uint16_t)src[1] << 8));
	return (int16_t)value;
}

static void HostDirectOperation_WriteI16Be(uint8_t* dst, int16_t value)
{
	uint16_t raw = (uint16_t)value;
	dst[0] = (uint8_t)((raw >> 8) & 0xFFU);
	dst[1] = (uint8_t)(raw & 0xFFU);
}

static uint16_t HostDirectOperation_MapTxStatusToHostError(ExecutorCommandTxStatus_t status)
{
	switch (status) {
		case EXECUTOR_COMMAND_TX_DUPLICATE_TRANSACTION:
		case EXECUTOR_COMMAND_TX_QUEUE_FULL:
			return HOST_ERR_BUSY;

		case EXECUTOR_COMMAND_TX_INVALID_ARG:
		case EXECUTOR_COMMAND_TX_INVALID_FRAME:
			return HOST_ERR_INVALID_PARAM;

		case EXECUTOR_COMMAND_TX_QUEUE_SEND_FAILED:
		case EXECUTOR_COMMAND_TX_ROUTE_REGISTRATION_FAILED:
		default:
			return HOST_ERR_HARDWARE;
	}
}

static uint16_t HostDirectOperation_MapNackToHostError(
		const ExecutorTransactionUpdate_t* update)
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

static bool HostDirectOperation_IsExecutorBackedReady(uint16_t host_command_code)
{
	SystemState_t state = GetSystemState();

	if (state == SYS_STATE_BUSY || state == SYS_STATE_INITIALIZING) {
		Dispatcher_SendError(host_command_code, HOST_ERR_BUSY);
		return false;
	}

	if (state != SYS_STATE_READY) {
		Dispatcher_SendError(host_command_code, HOST_ERR_NOT_INIT);
		return false;
	}

	if (JobManager_HasActiveJob()) {
		Dispatcher_SendError(host_command_code, HOST_ERR_BUSY);
		return false;
	}

	if (!ServiceManager_IsNodeOnline(CAN_ADDR_THERMO_BOARD)) {
		Dispatcher_SendError(host_command_code, HOST_ERR_NOT_INIT);
		Dispatcher_SendUsbResponse("ERROR: Thermo executor is offline.");
		return false;
	}

	if (ServiceManager_HasActiveOperationForNode(CAN_ADDR_THERMO_BOARD)) {
		Dispatcher_SendError(host_command_code, HOST_ERR_BUSY);
		Dispatcher_SendUsbResponse("ERROR: Thermo service operation is active.");
		return false;
	}

	return true;
}

static bool HostDirectOperation_StartCommon(HostDirectOperationKind_t kind,
                                            uint16_t host_command_code,
                                            uint8_t sensor_id,
                                            uint8_t expected_channel)
{
	if (!HostDirectOperation_IsExecutorBackedReady(host_command_code)) {
		return false;
	}

	bool accepted = false;

	taskENTER_CRITICAL();
	if (g_host_direct_state == HOST_DIRECT_STATE_IDLE) {
		g_host_direct_state = HOST_DIRECT_STATE_START_REQUESTED;
		g_host_direct_kind = kind;
		g_host_direct_operation_id = HostDirectOperation_NextOperationId();
		g_host_direct_host_command_code = host_command_code;
		g_host_direct_sensor_id = sensor_id;
		g_host_direct_expected_channel = expected_channel;
		HostDirectOperation_ClearData();
		accepted = true;
	}
	taskEXIT_CRITICAL();

	if (!accepted) {
		Dispatcher_SendError(host_command_code, HOST_ERR_BUSY);
		return false;
	}

	SetSystemBusy();
	return true;
}

static void HostDirectOperation_ResetState(void)
{
	taskENTER_CRITICAL();
	g_host_direct_state = HOST_DIRECT_STATE_IDLE;
	g_host_direct_kind = HOST_DIRECT_KIND_NONE;
	g_host_direct_host_command_code = 0U;
	g_host_direct_sensor_id = 0U;
	g_host_direct_expected_channel = 0U;
	HostDirectOperation_ClearData();
	taskEXIT_CRITICAL();
}

static void HostDirectOperation_Finish(uint16_t host_status_code,
                                       bool send_done,
                                       bool restore_ready)
{
	uint16_t host_command_code = g_host_direct_host_command_code;

	ExecutorTransactionTable_ResetOperation(
			&g_host_direct_transactions,
			TX_OWNER_HOST_DIRECT_OPERATION,
			g_host_direct_operation_id);
	HostDirectOperation_DrainPendingResponses();
	HostDirectOperation_ResetState();

	if (host_command_code != 0U) {
		if (send_done) {
			Dispatcher_SendDone(host_command_code, HOST_STATUS_OK);
		}
		else {
			Dispatcher_SendError(host_command_code,
			                     host_status_code != HOST_STATUS_OK
			                             ? host_status_code
			                             : HOST_ERR_GENERAL);
		}
	}

	if (restore_ready && !SafetyOperation_IsLatchedOrActive()) {
		SetSystemReady();
	}
}

static void HostDirectOperation_BuildCommand(ExecutorCommandTxCommand_t* command)
{
	memset(command, 0, sizeof(*command));

	uint16_t low_command_code = CAN_CMD_THERMO_GET_TEMP;
	bool channel_valid = true;
	ExecutorTransactionResponsePolicy_t response_policy =
			EXECUTOR_TRANSACTION_RESPONSE_DATA_THEN_DONE;

	if (g_host_direct_kind == HOST_DIRECT_KIND_SENSOR_GET_ALL_TEMPS) {
		Packer_CreateGetAllTempsMsg(&command->frame);
		low_command_code = CAN_CMD_THERMO_GET_ALL;
		channel_valid = false;
		response_policy = EXECUTOR_TRANSACTION_RESPONSE_MULTI_DATA_THEN_DONE;
	}
	else {
		Packer_CreateGetTempMsg(g_host_direct_expected_channel, &command->frame);
	}

	command->transaction.route_owner = TX_OWNER_HOST_DIRECT_OPERATION;
	command->transaction.operation_id = g_host_direct_operation_id;
	command->transaction.host_command_code = g_host_direct_host_command_code;
	command->transaction.low_command_code = low_command_code;
	command->transaction.expected_node_id = CAN_ADDR_THERMO_BOARD;
	command->transaction.expected_channel = g_host_direct_expected_channel;
	command->transaction.channel_valid = channel_valid;
	command->transaction.response_policy = response_policy;
	command->transaction.operation_timeout_ms = HOST_DIRECT_THERMO_TIMEOUT_MS;
}

static void HostDirectOperation_Begin(void)
{
	taskENTER_CRITICAL();
	if (g_host_direct_state != HOST_DIRECT_STATE_START_REQUESTED) {
		taskEXIT_CRITICAL();
		return;
	}
	g_host_direct_state = HOST_DIRECT_STATE_RUNNING;
	taskEXIT_CRITICAL();

	HostDirectOperation_DrainPendingResponses();
	ExecutorTransactionTable_Reset(&g_host_direct_transactions);

	ExecutorCommandTxCommand_t command;
	HostDirectOperation_BuildCommand(&command);

	ExecutorCommandTxStatus_t tx_status =
			ExecutorCommandTx_SubmitBatch(&g_host_direct_transactions,
			                              &command,
			                              1U);
	if (tx_status != EXECUTOR_COMMAND_TX_OK) {
		HostDirectOperation_Finish(
				HostDirectOperation_MapTxStatusToHostError(tx_status),
				false,
				true);
	}
}

static bool HostDirectOperation_StoreGetTempData(const CAN_Response_t* response)
{
	if (response == NULL || response->data_len < 2U) {
		return false;
	}

	const uint8_t* raw = response->payload.raw;
	g_host_direct_single_temp_deci_c = HostDirectOperation_ReadI16Le(&raw[0]);
	g_host_direct_single_temp_valid = true;
	return true;
}

static bool HostDirectOperation_UpsertTempRecord(uint8_t sensor_id, int16_t temperature)
{
	for (uint8_t i = 0; i < g_host_direct_temp_record_count; i++) {
		HostDirectTempRecord_t* record = &g_host_direct_temp_records[i];
		if (record->sensor_id == sensor_id) {
			record->temperature_deci_c = temperature;
			record->status = HOST_DIRECT_TEMP_STATUS_NORMAL;
			return true;
		}
	}

	if (g_host_direct_temp_record_count >= HOST_DIRECT_THERMO_MAX_SENSORS) {
		return false;
	}

	HostDirectTempRecord_t* record =
			&g_host_direct_temp_records[g_host_direct_temp_record_count++];
	record->sensor_id = sensor_id;
	record->temperature_deci_c = temperature;
	record->status = HOST_DIRECT_TEMP_STATUS_NORMAL;
	return true;
}

static bool HostDirectOperation_StoreGetAllData(const CAN_Response_t* response)
{
	if (response == NULL || response->data_len < 3U) {
		return false;
	}

	uint8_t channel = response->payload.raw[0];
	if (channel >= HOST_DIRECT_THERMO_EXECUTOR_CHANNELS) {
		return false;
	}

	uint8_t sensor_id = (uint8_t)(channel + 1U);
	int16_t temperature = HostDirectOperation_ReadI16Le(&response->payload.raw[1]);

	return HostDirectOperation_UpsertTempRecord(sensor_id, temperature);
}

static bool HostDirectOperation_SendGetTempData(void)
{
	if (!g_host_direct_single_temp_valid) {
		return false;
	}

	uint8_t payload[4];
	payload[0] = g_host_direct_sensor_id;
	HostDirectOperation_WriteI16Be(&payload[1], g_host_direct_single_temp_deci_c);
	payload[3] = HOST_DIRECT_TEMP_STATUS_NORMAL;

	Dispatcher_SendData(g_host_direct_host_command_code,
	                    HOST_RESPONSE_TYPE_DATA,
	                    HOST_STATUS_OK,
	                    payload,
	                    sizeof(payload));
	return true;
}

static bool HostDirectOperation_SendGetAllData(void)
{
	if (g_host_direct_temp_record_count == 0U) {
		return false;
	}

	uint8_t payload[1U + (HOST_DIRECT_THERMO_MAX_SENSORS * 4U)];
	payload[0] = g_host_direct_temp_record_count;

	for (uint8_t i = 0; i < g_host_direct_temp_record_count; i++) {
		const HostDirectTempRecord_t* record = &g_host_direct_temp_records[i];
		uint8_t offset = (uint8_t)(1U + (i * 4U));

		payload[offset] = record->sensor_id;
		HostDirectOperation_WriteI16Be(&payload[offset + 1U],
		                               record->temperature_deci_c);
		payload[offset + 3U] = record->status;
	}

	Dispatcher_SendData(g_host_direct_host_command_code,
	                    HOST_RESPONSE_TYPE_DATA,
	                    HOST_STATUS_OK,
	                    payload,
	                    (uint16_t)(1U + (g_host_direct_temp_record_count * 4U)));
	return true;
}

static void HostDirectOperation_HandleData(const CanRoutedResponse_t* routed,
                                           const ExecutorTransactionUpdate_t* update)
{
	if (update == NULL || !update->matched) {
		return;
	}

	if (update->event == EXECUTOR_TRANSACTION_EVENT_PROTOCOL_ERROR) {
		HostDirectOperation_Finish(HOST_ERR_GENERAL, false, true);
		return;
	}

	if (update->event != EXECUTOR_TRANSACTION_EVENT_DATA_ACCEPTED) {
		return;
	}

	bool stored = false;
	if (g_host_direct_kind == HOST_DIRECT_KIND_SENSOR_GET_ALL_TEMPS) {
		stored = HostDirectOperation_StoreGetAllData(&routed->parsed);
	}
	else if (g_host_direct_kind == HOST_DIRECT_KIND_SENSOR_GET_TEMP) {
		stored = HostDirectOperation_StoreGetTempData(&routed->parsed);
	}

	if (!stored) {
		HostDirectOperation_Finish(HOST_ERR_PACKET_FORMAT, false, true);
	}
}

static void HostDirectOperation_HandleDone(const ExecutorTransactionUpdate_t* update)
{
	if (update == NULL || !update->matched) {
		return;
	}

	if (update->event == EXECUTOR_TRANSACTION_EVENT_PROTOCOL_ERROR) {
		HostDirectOperation_Finish(HOST_ERR_GENERAL, false, true);
		return;
	}

	if (update->event != EXECUTOR_TRANSACTION_EVENT_DONE) {
		return;
	}

	bool data_sent = false;
	if (g_host_direct_kind == HOST_DIRECT_KIND_SENSOR_GET_ALL_TEMPS) {
		data_sent = HostDirectOperation_SendGetAllData();
	}
	else if (g_host_direct_kind == HOST_DIRECT_KIND_SENSOR_GET_TEMP) {
		data_sent = HostDirectOperation_SendGetTempData();
	}

	if (!data_sent) {
		HostDirectOperation_Finish(HOST_ERR_GENERAL, false, true);
		return;
	}

	HostDirectOperation_Finish(HOST_STATUS_OK, true, true);
}

static void HostDirectOperation_HandleTransactionUpdate(
		const CanRoutedResponse_t* routed,
		const ExecutorTransactionUpdate_t* update)
{
	if (update == NULL || !update->matched) {
		return;
	}

	switch (update->event) {
		case EXECUTOR_TRANSACTION_EVENT_DATA_ACCEPTED:
		case EXECUTOR_TRANSACTION_EVENT_PROTOCOL_ERROR:
			HostDirectOperation_HandleData(routed, update);
			break;

		case EXECUTOR_TRANSACTION_EVENT_DONE:
			HostDirectOperation_HandleDone(update);
			break;

		case EXECUTOR_TRANSACTION_EVENT_NACK:
			HostDirectOperation_Finish(
					HostDirectOperation_MapNackToHostError(update),
					false,
					true);
			break;

		case EXECUTOR_TRANSACTION_EVENT_ACK_TIMEOUT:
		case EXECUTOR_TRANSACTION_EVENT_OPERATION_TIMEOUT:
			ServiceManager_StartRecovery(update->node_id);
			HostDirectOperation_Finish(HOST_ERR_TIMEOUT, false, false);
			break;

		case EXECUTOR_TRANSACTION_EVENT_ACKED:
		case EXECUTOR_TRANSACTION_EVENT_NONE:
		default:
			break;
	}
}

static void HostDirectOperation_ProcessRoutedResponse(const CanRoutedResponse_t* routed)
{
	if (routed == NULL ||
			!routed->context_valid ||
			routed->context_owner != TX_OWNER_HOST_DIRECT_OPERATION) {
		return;
	}

	ExecutorTransactionUpdate_t update;
	memset(&update, 0, sizeof(update));

	const CAN_Response_t* response = &routed->parsed;
	switch (response->msg_type) {
		case CAN_MSG_TYPE_ACK:
			update = ExecutorTransactionTable_HandleAck(
					&g_host_direct_transactions,
					routed);
			break;

		case CAN_MSG_TYPE_NACK:
			update = ExecutorTransactionTable_HandleNack(
					&g_host_direct_transactions,
					routed);
			break;

		case CAN_MSG_TYPE_DATA_DONE_LOG:
			if (response->sub_type == CAN_SUB_TYPE_DATA) {
				update = ExecutorTransactionTable_HandleData(
						&g_host_direct_transactions,
						routed);
			}
			else if (response->sub_type == CAN_SUB_TYPE_DONE) {
				update = ExecutorTransactionTable_HandleDone(
						&g_host_direct_transactions,
						routed);
			}
			else {
				return;
			}
			break;

		default:
			return;
	}

	HostDirectOperation_HandleTransactionUpdate(routed, &update);
}

static void HostDirectOperation_DrainRoutedResponses(void)
{
	CanRoutedResponse_t routed;
	while (xQueueReceive(can_host_direct_rx_queue_handle, &routed, 0) == pdPASS) {
		HostDirectOperation_ProcessRoutedResponse(&routed);
		if (g_host_direct_state != HOST_DIRECT_STATE_RUNNING) {
			break;
		}
	}
}

static void HostDirectOperation_CheckTimeouts(void)
{
	ExecutorTransactionUpdate_t update;
	if (ExecutorTransactionTable_CheckTimeouts(&g_host_direct_transactions,
	                                           HAL_GetTick(),
	                                           &update)) {
		HostDirectOperation_HandleTransactionUpdate(NULL, &update);
	}
}

void HostDirectOperation_Init(void)
{
	taskENTER_CRITICAL();
	g_host_direct_state = HOST_DIRECT_STATE_IDLE;
	g_host_direct_kind = HOST_DIRECT_KIND_NONE;
	g_host_direct_operation_id = HOST_DIRECT_OPERATION_FIRST_ID;
	g_host_direct_next_operation_id = HOST_DIRECT_OPERATION_FIRST_ID;
	g_host_direct_host_command_code = 0U;
	g_host_direct_sensor_id = 0U;
	g_host_direct_expected_channel = 0U;
	HostDirectOperation_ClearData();
	taskEXIT_CRITICAL();

	ExecutorTransactionTable_Reset(&g_host_direct_transactions);
	HostDirectOperation_DrainPendingResponses();
}

void HostDirectOperation_StartThermoGetTemp(uint16_t host_command_code,
                                            const uint8_t* params,
                                            uint16_t params_len)
{
	(void)params;
	(void)params_len;

	Dispatcher_SendError(host_command_code, HOST_ERR_NOT_SUPPORTED);
	Dispatcher_SendUsbResponse(
			"ERROR: THERMO_GET_TEMP is legacy thermostat API and is not used by current Thermo sensors path.");
}

void HostDirectOperation_StartSensorGetAllTemps(uint16_t host_command_code,
                                                const uint8_t* params,
                                                uint16_t params_len)
{
	(void)params;

	if (params_len != 0U) {
		Dispatcher_SendError(host_command_code, HOST_ERR_INVALID_PARAM);
		return;
	}

	(void)HostDirectOperation_StartCommon(
			HOST_DIRECT_KIND_SENSOR_GET_ALL_TEMPS,
			host_command_code,
			0U,
			0U);
}

void HostDirectOperation_StartSensorGetTemp(uint16_t host_command_code,
                                            const uint8_t* params,
                                            uint16_t params_len)
{
	if (params == NULL || params_len != 1U) {
		Dispatcher_SendError(host_command_code, HOST_ERR_INVALID_PARAM);
		return;
	}

	uint8_t sensor_id = params[0];
	if (sensor_id == 0U || sensor_id > HOST_DIRECT_THERMO_EXECUTOR_CHANNELS) {
		Dispatcher_SendError(host_command_code, HOST_ERR_INVALID_PARAM);
		return;
	}

	uint8_t channel = (uint8_t)(sensor_id - 1U);

	(void)HostDirectOperation_StartCommon(
			HOST_DIRECT_KIND_SENSOR_GET_TEMP,
			host_command_code,
			sensor_id,
			channel);
}

void HostDirectOperation_Run(void)
{
	if (g_host_direct_state == HOST_DIRECT_STATE_IDLE) {
		return;
	}

	if (SafetyOperation_IsLatchedOrActive()) {
		HostDirectOperation_Finish(HOST_ERR_EMERGENCY_STOP, false, false);
		return;
	}

	if (g_host_direct_state == HOST_DIRECT_STATE_START_REQUESTED) {
		HostDirectOperation_Begin();
	}

	if (g_host_direct_state != HOST_DIRECT_STATE_RUNNING) {
		return;
	}

	if (can_host_direct_rx_queue_handle == NULL) {
		HostDirectOperation_Finish(HOST_ERR_GENERAL, false, true);
		return;
	}

	HostDirectOperation_DrainRoutedResponses();
	if (g_host_direct_state != HOST_DIRECT_STATE_RUNNING) {
		return;
	}

	HostDirectOperation_CheckTimeouts();
}

bool HostDirectOperation_IsActive(void)
{
	bool active;

	taskENTER_CRITICAL();
	active = (g_host_direct_state != HOST_DIRECT_STATE_IDLE);
	taskEXIT_CRITICAL();

	return active;
}
