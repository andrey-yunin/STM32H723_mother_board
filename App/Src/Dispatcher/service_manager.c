/*
 * service_manager.c
 *
 *  Created on: Apr 13, 2026
 *      Author: andrey
 */

#include "Dispatcher/service_manager.h"
#include "Dispatcher/can_packer.h"
#include "Dispatcher/executor_command_tx.h"
#include "Dispatcher/dispatcher_io.h"
#include "shared_resources.h"
#include "app_config.h"
#include "main.h"
#include "Dispatcher/device_mapping.h"
#include "task_dispatcher.h"
#include <string.h>
#include <stdio.h>
#include "Dispatcher/can_response_router.h"
#include "Tasks/task_logger.h"

/*
 * ServiceManager владеет только внутренними service-операциями Дирижера:
 * discovery/inventory, сверка UID и диагностический GET_STATUS baseline/delta.
 * Host recipe/direct job здесь не продвигаются: service responses маршрутизируются
 * отдельно через can_service_rx_queue_handle.
 */

/*
 * Окно discovery и счетчик operation id.
 * Используются ServiceManager_StartDiscovery() и ServiceManager_Run().
 */
#define SERVICE_MANAGER_DISCOVERY_WINDOW_MS       500U
#define SERVICE_MANAGER_OPERATION_NONE            0U
#define SERVICE_MANAGER_FIRST_OPERATION_ID        1U

/*
 * Форматы multi-frame service DATA.
 * Используются обработчиками F001/F004/F007 ниже.
 */
#define SERVICE_UID_SIZE_BYTES                    12U
#define SERVICE_UID_WORD_COUNT                    3U
#define SERVICE_CAN_DATA_PAYLOAD_BYTES            6U

#define SERVICE_F001_MIN_FIRST_DATA_BYTES         4U
#define SERVICE_F001_REQUIRED_DATA_FRAMES         3U
#define SERVICE_F001_FIRST_UID_OFFSET             4U
#define SERVICE_F001_SECOND_UID_OFFSET            2U
#define SERVICE_F001_THIRD_UID_OFFSET             8U
#define SERVICE_F001_FRAME_DEVICE_INFO            0U
#define SERVICE_F001_FRAME_UID_MIDDLE             1U
#define SERVICE_F001_FRAME_UID_TAIL               2U

#define SERVICE_F004_REQUIRED_DATA_FRAMES         2U

/*
 * Common GET_STATUS metric IDs из dds240_global_config.h.
 * Локальная копия в firmware нужна для разбора F007 без подключения
 * внешнего документа как include.
 */
#define SERVICE_STATUS_RX_TOTAL                   0x0001U
#define SERVICE_STATUS_TX_TOTAL                   0x0002U
#define SERVICE_STATUS_RX_QUEUE_OVERFLOW          0x0003U
#define SERVICE_STATUS_TX_QUEUE_OVERFLOW          0x0004U
#define SERVICE_STATUS_DISPATCHER_OVERFLOW        0x0005U
#define SERVICE_STATUS_DROP_NOT_EXT               0x0006U
#define SERVICE_STATUS_DROP_WRONG_DST             0x0007U
#define SERVICE_STATUS_DROP_WRONG_TYPE            0x0008U
#define SERVICE_STATUS_DROP_WRONG_DLC             0x0009U
#define SERVICE_STATUS_TX_MAILBOX_TIMEOUT         0x000AU
#define SERVICE_STATUS_TX_HAL_ERROR               0x000BU
#define SERVICE_STATUS_ERROR_CALLBACK             0x000CU
#define SERVICE_STATUS_ERROR_WARNING              0x000DU
#define SERVICE_STATUS_ERROR_PASSIVE              0x000EU
#define SERVICE_STATUS_BUS_OFF                    0x000FU
#define SERVICE_STATUS_LAST_HAL_ERROR             0x0010U
#define SERVICE_STATUS_LAST_ESR                   0x0011U
#define SERVICE_STATUS_APP_QUEUE_OVERFLOW         0x0012U
#define SERVICE_STATUS_FIRST_METRIC_ID            SERVICE_STATUS_RX_TOTAL
#define SERVICE_STATUS_LAST_METRIC_ID             SERVICE_STATUS_APP_QUEUE_OVERFLOW
#define SERVICE_STATUS_METRIC_COUNT               (SERVICE_STATUS_LAST_METRIC_ID - SERVICE_STATUS_FIRST_METRIC_ID + 1U)
#define SERVICE_STATUS_REQUIRED_MASK              ((1UL << SERVICE_STATUS_METRIC_COUNT) - 1UL)

/*
 * Per-node state machine service-а.
 * Пишется обработчиками F001/F004/F007, читается stale/late проверкой
 * ServiceManager_ResponseIsCurrent().
 */
typedef enum {
	SERVICE_NODE_STAGE_IDLE = 0,
	SERVICE_NODE_STAGE_DISCOVERING,
	SERVICE_NODE_STAGE_INFO_DONE,
	SERVICE_NODE_STAGE_UID_REQUESTED,
	SERVICE_NODE_STAGE_UID_DONE,
	SERVICE_NODE_STAGE_STATUS_REQUESTED,
	SERVICE_NODE_STAGE_READY,
	SERVICE_NODE_STAGE_ERROR
} ServiceNodeStage_t;

/*
 * Runtime-состояние service-а на один physical NodeID.
 * Индекс всегда совпадает с g_inventory[].
 */
typedef struct {
	ServiceNodeStage_t stage;
	uint32_t active_operation_id;
	uint8_t f001_data_count;
	uint8_t f004_data_count;
	uint8_t uid_bytes[SERVICE_UID_SIZE_BYTES];
	uint32_t status_current[SERVICE_STATUS_METRIC_COUNT];
	uint32_t status_last[SERVICE_STATUS_METRIC_COUNT];
	uint32_t status_seen_mask;
	bool status_baseline_valid;
} ServiceNodeRuntime_t;

/*
 * Основное runtime-хранилище ServiceManager.
 * Доступ идет из task_jobs_monitor через ServiceManager_Run() и из startup
 * через ServiceManager_StartDiscovery().
 */
static DeviceNode_t g_inventory[MAX_DISCOVERED_NODES];
static ServiceNodeRuntime_t g_node_runtime[MAX_DISCOVERED_NODES];
static uint8_t g_nodes_count = 0;
static ExecutorTransactionTable_t g_service_transactions;

static bool g_discovery_window_active = false;
static uint32_t g_discovery_started_ms = 0U;
static uint32_t g_discovery_operation_id = SERVICE_MANAGER_OPERATION_NONE;
static uint32_t g_next_service_operation_id = SERVICE_MANAGER_FIRST_OPERATION_ID;
static uint32_t g_late_service_response_count = 0U;
static bool g_recovery_active = false;
static uint8_t g_recovery_target_node_id = CAN_ADDR_BROADCAST;

/*
 * Wrap-safe проверка timeout.
 * Вызывается ServiceManager_ResponseIsCurrent() и
 * ServiceManager_CheckDiscoveryWindowTimeout().
 */
static bool ServiceManager_TickElapsed(uint32_t start_ms, uint32_t timeout_ms)
{
	return (uint32_t)(HAL_GetTick() - start_ms) >= timeout_ms;
}

/*
 * Little-endian helpers для payload executor-а.
 * Вызываются UID/status парсерами: ServiceManager_ApplyUidBytes()
 * и ServiceManager_HandleGetStatusData().
 */
static uint16_t ServiceManager_ReadU16Le(const uint8_t* src)
{
	return (uint16_t)(src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t ServiceManager_ReadU32Le(const uint8_t* src)
{
	return (uint32_t)src[0] |
			((uint32_t)src[1] << 8) |
			((uint32_t)src[2] << 16) |
			((uint32_t)src[3] << 24);
}

/*
 * Генератор operation id для service transactions.
 * Вызывается ServiceManager_StartDiscovery() и
 * ServiceManager_StartNodeServiceCommand().
 */
static uint32_t ServiceManager_NextOperationId(void)
{
	uint32_t operation_id = g_next_service_operation_id++;

	if (g_next_service_operation_id == SERVICE_MANAGER_OPERATION_NONE) {
		g_next_service_operation_id = SERVICE_MANAGER_FIRST_OPERATION_ID;
	}

	return operation_id;
}

/*
 * Единый вход текстовой диагностики service layer в task_logger/log_queue.
 * Вызывается всеми ServiceManager_Log* helpers и F007 baseline/delta path.
 */
static void ServiceManager_LogText(const char* text)
{
	if (text != NULL) {
		(void)Logger_LogText(text);
	}
}

/*
 * Лог ошибки outbound API.
 * Вызывается ServiceManager_SubmitServiceCommand().
 */
static void ServiceManager_LogTxFailure(
		uint16_t command_code,
		uint8_t expected_node_id,
		ExecutorCommandTxStatus_t status)
{
	char msg[APP_LOG_MESSAGE_MAX_LEN];
	snprintf(msg, sizeof(msg),
			"ERROR: Service command TX failed: cmd=0x%04X dst=0x%02X status=%s.",
			command_code,
			expected_node_id,
			ExecutorCommandTx_StatusString(status));
	ServiceManager_LogText(msg);
}

/*
 * Лог terminal fault по конкретному узлу.
 * Вызывается ServiceManager_MarkNodeError() и редкими ветками без node runtime.
 */
static void ServiceManager_LogNodeError(
		uint8_t node_id,
		uint16_t command_code,
		const char* reason)
{
	char msg[APP_LOG_MESSAGE_MAX_LEN];
	snprintf(msg, sizeof(msg),
			"ERROR: Service node error: node=0x%02X cmd=0x%04X reason=%s.",
			node_id,
			command_code,
			reason != NULL ? reason : "unknown");
	ServiceManager_LogText(msg);
}

/*
 * Лог late/stale/service-protocol response, который не должен менять inventory
 * и не должен продвигать Host job.
 * Вызывается ServiceManager_Run() и внутренними обработчиками ACK/DATA/DONE.
 */
static void ServiceManager_LogIgnoredResponse(
		const CanRoutedResponse_t* routed,
		const char* reason)
{
	if (routed == NULL) {
		return;
	}

	const CAN_Response_t* response = &routed->parsed;
	g_late_service_response_count++;

	char msg[APP_LOG_MESSAGE_MAX_LEN];
	snprintf(msg, sizeof(msg),
			"WARNING: Service response ignored: src=0x%02X type=%u sub=%u cmd=0x%04X op=%lu late=%lu (%s).",
			response->source_addr,
			response->msg_type,
			response->sub_type,
			routed->context_valid ? routed->context_command_code : response->command_code,
			(unsigned long)(routed->context_valid ? routed->context_job_id : 0U),
			(unsigned long)g_late_service_response_count,
			reason != NULL ? reason : "stale context");
	ServiceManager_LogText(msg);
}

/*
 * Поиск существующего узла в inventory.
 * Вызывается stale/late проверкой и всеми F001/F004/F007 обработчиками.
 */
static int ServiceManager_FindNodeIndex(uint8_t node_id)
{
	for (uint8_t i = 0; i < g_nodes_count; i++) {
		if (g_inventory[i].node_id == node_id) {
			return (int)i;
		}
	}

	return -1;
}

/*
 * Сброс volatile runtime одного inventory slot-а.
 * Вызывается ServiceManager_Init(), ServiceManager_StartDiscovery()
 * и ServiceManager_EnsureNodeIndex().
 */
static void ServiceManager_ResetNodeRuntime(uint8_t node_index)
{
	if (node_index >= MAX_DISCOVERED_NODES) {
		return;
	}

	memset(&g_node_runtime[node_index], 0, sizeof(g_node_runtime[node_index]));
	g_node_runtime[node_index].stage = SERVICE_NODE_STAGE_IDLE;
}

/*
 * Создает inventory slot для нового source NodeID, если его еще нет.
 * Вызывается F001 DATA/NACK path и legacy ServiceManager_UpdateNode().
 */
static int ServiceManager_EnsureNodeIndex(uint8_t node_id)
{
	int node_index = ServiceManager_FindNodeIndex(node_id);

	if (node_index >= 0) {
		return node_index;
	}

	if (g_nodes_count >= MAX_DISCOVERED_NODES) {
		char msg[APP_LOG_MESSAGE_MAX_LEN];
		snprintf(msg, sizeof(msg),
				"ERROR: Service inventory full, node=0x%02X ignored.",
				node_id);
		ServiceManager_LogText(msg);
		return -1;
	}

	node_index = (int)g_nodes_count++;
	memset(&g_inventory[node_index], 0, sizeof(g_inventory[node_index]));
	g_inventory[node_index].node_id = node_id;
	ServiceManager_ResetNodeRuntime((uint8_t)node_index);

	return node_index;
}

/*
 * Проверка, есть ли активная per-node service transaction.
 * Вызывается ServiceManager_StartDiscovery(), чтобы не начать новый broadcast
 * поверх незавершенной F004/F007 цепочки.
 */
static bool ServiceManager_HasActiveNodeOperations(void)
{
	for (uint8_t i = 0; i < g_nodes_count; i++) {
		if (g_node_runtime[i].active_operation_id != SERVICE_MANAGER_OPERATION_NONE) {
			return true;
		}
	}

	return false;
}

/*
 * Закрывает активные service transactions перед принудительным recovery discovery.
 * Вызывается ServiceManager_StartDiscoveryInternal(true), чтобы stale F004/F007
 * routes не могли продвинуть новый recovery cycle.
 */
static void ServiceManager_ResetActiveServiceOperations(void)
{
	if (g_discovery_operation_id != SERVICE_MANAGER_OPERATION_NONE) {
		CanResponseRouter_CloseOperation(
				TX_OWNER_SERVICE_INTERNAL,
				g_discovery_operation_id);
	}

	for (uint8_t i = 0; i < g_nodes_count; i++) {
		ServiceNodeRuntime_t* runtime = &g_node_runtime[i];

		if (runtime->active_operation_id != SERVICE_MANAGER_OPERATION_NONE) {
			ExecutorTransactionTable_ResetOperation(
					&g_service_transactions,
					TX_OWNER_SERVICE_INTERNAL,
					runtime->active_operation_id);
			runtime->active_operation_id = SERVICE_MANAGER_OPERATION_NONE;
		}
	}

	ExecutorTransactionTable_Reset(&g_service_transactions);
	g_discovery_window_active = false;
	g_discovery_operation_id = SERVICE_MANAGER_OPERATION_NONE;
}

/*
 * Копирование UID-фрагментов из DATA payload.
 * Вызывается ServiceManager_HandleGetInfoData() и
 * ServiceManager_HandleGetUidData().
 */
static void ServiceManager_CopyUidBytes(
		uint8_t node_index,
		uint8_t offset,
		const uint8_t* src,
		uint8_t src_len)
{
	if (node_index >= MAX_DISCOVERED_NODES ||
			src == NULL ||
			offset >= SERVICE_UID_SIZE_BYTES) {
		return;
	}

	uint8_t copy_len = (uint8_t)(SERVICE_UID_SIZE_BYTES - offset);
	if (src_len < copy_len) {
		copy_len = src_len;
	}

	memcpy(&g_node_runtime[node_index].uid_bytes[offset], src, copy_len);
}

/*
 * Перевод 12 UID bytes в три uint32_t inventory words.
 * Вызывается F001 DONE и F004 DONE обработчиками.
 */
static void ServiceManager_ApplyUidBytes(uint8_t node_index, uint32_t* out_uid)
{
	if (node_index >= MAX_DISCOVERED_NODES || out_uid == NULL) {
		return;
	}

	const uint8_t* uid = g_node_runtime[node_index].uid_bytes;

	for (uint8_t i = 0; i < SERVICE_UID_WORD_COUNT; i++) {
		out_uid[i] = ServiceManager_ReadU32Le(&uid[i * sizeof(uint32_t)]);
	}
}

/*
 * Сравнение UID из F004 с UID, собранным ранее из F001.
 * Вызывается ServiceManager_HandleGetUidDone().
 */
static bool ServiceManager_UidMatchesNode(uint8_t node_index, const uint32_t* uid)
{
	if (node_index >= g_nodes_count || uid == NULL) {
		return false;
	}

	for (uint8_t i = 0; i < SERVICE_UID_WORD_COUNT; i++) {
		if (g_inventory[node_index].uid[i] != uid[i]) {
			return false;
		}
	}

	return true;
}

/*
 * Terminal fault для одного узла: закрыть его active service operation,
 * пометить offline и залогировать причину.
 * Вызывается обработчиками F001/F004/F007 и service timeout path.
 */
static void ServiceManager_MarkNodeError(
		uint8_t node_index,
		uint16_t command_code,
		const char* reason)
{
	if (node_index >= g_nodes_count) {
		return;
	}

	ServiceNodeRuntime_t* runtime = &g_node_runtime[node_index];

	if (runtime->active_operation_id != SERVICE_MANAGER_OPERATION_NONE) {
		if (runtime->active_operation_id != g_discovery_operation_id) {
			ExecutorTransactionTable_ResetOperation(
					&g_service_transactions,
					TX_OWNER_SERVICE_INTERNAL,
					runtime->active_operation_id);
		}
		runtime->active_operation_id = SERVICE_MANAGER_OPERATION_NONE;
	}

	runtime->stage = SERVICE_NODE_STAGE_ERROR;
	g_inventory[node_index].is_online = false;

	ServiceManager_LogNodeError(
			g_inventory[node_index].node_id,
			command_code,
			reason);
}

/*
 * Завершает незакрытые F001 nodes при timeout discovery window.
 * Вызывается ServiceManager_CloseDiscoveryWindow().
 */
static void ServiceManager_ExpireDiscoveryNodes(uint32_t operation_id)
{
	if (operation_id == SERVICE_MANAGER_OPERATION_NONE) {
		return;
	}

	for (uint8_t i = 0; i < g_nodes_count; i++) {
		if (g_node_runtime[i].active_operation_id == operation_id) {
			ServiceManager_MarkNodeError(
					i,
					CAN_CMD_SRV_GET_INFO,
					"discovery window timeout");
		}
	}
}

/*
 * Единая точка отправки service CAN command через outbound TX boundary.
 * Вызывается ServiceManager_StartDiscovery() и
 * ServiceManager_StartNodeServiceCommand().
 */
static bool ServiceManager_SubmitServiceCommand(
		const CAN_Message_t* frame,
		uint16_t command_code,
		uint8_t expected_node_id,
		uint32_t operation_id)
{
	if (frame == NULL || operation_id == SERVICE_MANAGER_OPERATION_NONE) {
		return false;
	}

	ExecutorCommandTxCommand_t command;
	memset(&command, 0, sizeof(command));

	command.frame = *frame;
	command.transaction.route_owner = TX_OWNER_SERVICE_INTERNAL;
	command.transaction.operation_id = operation_id;
	command.transaction.host_command_code = 0U;
	command.transaction.low_command_code = command_code;
	command.transaction.expected_node_id = expected_node_id;
	command.transaction.expected_channel = 0U;
	command.transaction.channel_valid = false;
	command.transaction.response_policy =
			EXECUTOR_TRANSACTION_RESPONSE_MULTI_DATA_THEN_DONE;
	command.transaction.operation_timeout_ms = JOB_TIMEOUT_MS;

	ExecutorCommandTxStatus_t status = ExecutorCommandTx_SubmitBatch(
			&g_service_transactions,
			&command,
			1U);

	if (status != EXECUTOR_COMMAND_TX_OK) {
		ServiceManager_LogTxFailure(command_code, expected_node_id, status);
		return false;
	}

	return true;
}

/*
 * Запуск unicast service-команды для конкретного узла.
 * Вызывается из цепочки:
 * - F001 DONE -> F004 GET_UID;
 * - F004 DONE -> F007 GET_STATUS.
 */
static bool ServiceManager_StartNodeServiceCommand(
		uint8_t node_index,
		uint16_t command_code)
{
	if (node_index >= g_nodes_count) {
		return false;
	}

	CAN_Message_t msg;
	uint8_t node_id = g_inventory[node_index].node_id;

	switch (command_code) {
		case CAN_CMD_SRV_GET_UID:
			Packer_CreateGetUidMsg(node_id, &msg);
			break;

		case CAN_CMD_SRV_GET_STATUS:
			Packer_CreateGetStatusMsg(node_id, &msg);
			break;

		default:
			return false;
	}

	uint32_t operation_id = ServiceManager_NextOperationId();

	if (!ServiceManager_SubmitServiceCommand(
			&msg,
			command_code,
			node_id,
			operation_id)) {
		ServiceManager_MarkNodeError(node_index, command_code, "TX failed");
		return false;
	}

	ServiceNodeRuntime_t* runtime = &g_node_runtime[node_index];
	runtime->active_operation_id = operation_id;

	if (command_code == CAN_CMD_SRV_GET_UID) {
		runtime->stage = SERVICE_NODE_STAGE_UID_REQUESTED;
		runtime->f004_data_count = 0U;
		memset(runtime->uid_bytes, 0, sizeof(runtime->uid_bytes));
	}
	else {
		runtime->stage = SERVICE_NODE_STAGE_STATUS_REQUESTED;
		memset(runtime->status_current, 0, sizeof(runtime->status_current));
		runtime->status_seen_mask = 0U;
	}

	return true;
}

/*
 * Stale/late gate для всех service responses.
 * Вызывается ServiceManager_Run() перед передачей routed response
 * в ACK/NACK/DATA/DONE обработчики.
 */
static bool ServiceManager_ResponseIsCurrent(const CanRoutedResponse_t* routed)
{
	if (routed == NULL ||
			!routed->context_valid ||
			routed->context_owner != TX_OWNER_SERVICE_INTERNAL) {
		return false;
	}

	switch (routed->context_command_code) {
		case CAN_CMD_SRV_GET_INFO:
			return g_discovery_window_active &&
					routed->context_job_id == g_discovery_operation_id &&
					!ServiceManager_TickElapsed(
							g_discovery_started_ms,
							SERVICE_MANAGER_DISCOVERY_WINDOW_MS);

		case CAN_CMD_SRV_GET_UID: {
			int node_index = ServiceManager_FindNodeIndex(routed->parsed.source_addr);
			return node_index >= 0 &&
					g_node_runtime[node_index].stage == SERVICE_NODE_STAGE_UID_REQUESTED &&
					g_node_runtime[node_index].active_operation_id == routed->context_job_id;
		}

		case CAN_CMD_SRV_GET_STATUS: {
			int node_index = ServiceManager_FindNodeIndex(routed->parsed.source_addr);
			return node_index >= 0 &&
					g_node_runtime[node_index].stage == SERVICE_NODE_STAGE_STATUS_REQUESTED &&
					g_node_runtime[node_index].active_operation_id == routed->context_job_id;
		}

		default:
			return false;
	}
}

/*
 * Закрытие broadcast discovery operation: незавершенные F001 nodes переводит
 * в ERROR, затем закрывает router routes/templates.
 * Вызывается timeout path и веткой TX failure при старте discovery.
 */
static void ServiceManager_CloseDiscoveryWindow(void)
{
	if (!g_discovery_window_active ||
			g_discovery_operation_id == SERVICE_MANAGER_OPERATION_NONE) {
		return;
	}

	uint32_t operation_id = g_discovery_operation_id;

	ServiceManager_ExpireDiscoveryNodes(operation_id);

	CanResponseRouter_CloseOperation(
			TX_OWNER_SERVICE_INTERNAL,
			operation_id);

	g_discovery_window_active = false;
	g_discovery_operation_id = SERVICE_MANAGER_OPERATION_NONE;
}

/*
 * Контроль окончания discovery window.
 * Вызывается ServiceManager_Run() до и после draining service queue.
 */
static void ServiceManager_CheckDiscoveryWindowTimeout(void)
{
	if (g_discovery_window_active &&
			ServiceManager_TickElapsed(
					g_discovery_started_ms,
					SERVICE_MANAGER_DISCOVERY_WINDOW_MS)) {
		ServiceManager_CloseDiscoveryWindow();
		ServiceManager_LogText("INFO: Service discovery window closed.");
	}
}

/*
 * Контроль timeout-ов unicast F004/F007 transactions.
 * Вызывается ServiceManager_Run() до и после draining service queue.
 */
static void ServiceManager_HandleTransactionTimeouts(void)
{
	ExecutorTransactionUpdate_t update;

	while (ExecutorTransactionTable_CheckTimeouts(
			&g_service_transactions,
			HAL_GetTick(),
			&update)) {
		int node_index = ServiceManager_FindNodeIndex(update.node_id);

		if (node_index >= 0) {
			ServiceManager_MarkNodeError(
					(uint8_t)node_index,
					update.low_command_code,
					update.reason != NULL ? update.reason : "service timeout");
		}

		if (update.operation_id != SERVICE_MANAGER_OPERATION_NONE) {
			ExecutorTransactionTable_ResetOperation(
					&g_service_transactions,
					TX_OWNER_SERVICE_INTERNAL,
					update.operation_id);
		}
	}
}

/*
 * Завершает recovery cycle после timeout Host-operation.
 * Успехом считается повторное прохождение target node до SERVICE_NODE_STAGE_READY.
 * Если service operations закончились, а target node не READY, система остается
 * в ERROR до операторского/Host-level восстановления.
 */
static void ServiceManager_CheckRecoveryComplete(void)
{
	if (!g_recovery_active) {
		return;
	}

	int target_index = ServiceManager_FindNodeIndex(g_recovery_target_node_id);
	if (!g_discovery_window_active &&
			!ServiceManager_HasActiveNodeOperations() &&
			target_index >= 0 &&
			g_inventory[target_index].is_online &&
			g_node_runtime[target_index].stage == SERVICE_NODE_STAGE_READY) {
		char msg[APP_LOG_MESSAGE_MAX_LEN];
		snprintf(msg, sizeof(msg),
				"INFO: Service recovery complete: node=0x%02X.",
				g_recovery_target_node_id);
		ServiceManager_LogText(msg);

		g_recovery_active = false;
		g_recovery_target_node_id = CAN_ADDR_BROADCAST;
		SetSystemReady();
		return;
	}

	if (!g_discovery_window_active && !ServiceManager_HasActiveNodeOperations()) {
		char msg[APP_LOG_MESSAGE_MAX_LEN];
		snprintf(msg, sizeof(msg),
				"ERROR: Service recovery failed: node=0x%02X not ready.",
				g_recovery_target_node_id);
		ServiceManager_LogText(msg);

		g_recovery_active = false;
		g_recovery_target_node_id = CAN_ADDR_BROADCAST;
		SetSystemError(HOST_ERR_NOT_INIT);
	}
}

/*
 * DATA handler для F001 GET_DEVICE_INFO.
 * Вызывается ServiceManager_HandleData() только после stale/late gate.
 */
static bool ServiceManager_HandleGetInfoData(const CAN_Response_t* response)
{
	if (response == NULL || response->data_len < SERVICE_F001_MIN_FIRST_DATA_BYTES) {
		return false;
	}

	int node_index = ServiceManager_EnsureNodeIndex(response->source_addr);
	if (node_index < 0) {
		return false;
	}

	DeviceNode_t* node = &g_inventory[node_index];
	ServiceNodeRuntime_t* runtime = &g_node_runtime[node_index];

	if (runtime->stage == SERVICE_NODE_STAGE_ERROR) {
		return false;
	}

	if (runtime->stage == SERVICE_NODE_STAGE_IDLE) {
		runtime->stage = SERVICE_NODE_STAGE_DISCOVERING;
		runtime->active_operation_id = g_discovery_operation_id;
		memset(runtime->uid_bytes, 0, sizeof(runtime->uid_bytes));
	}

	if (runtime->f001_data_count >= SERVICE_F001_REQUIRED_DATA_FRAMES) {
		ServiceManager_MarkNodeError(
				(uint8_t)node_index,
				CAN_CMD_SRV_GET_INFO,
				"extra F001 DATA");
		return false;
	}

	switch (runtime->f001_data_count) {
		case SERVICE_F001_FRAME_DEVICE_INFO:
			node->device_type = response->payload.raw[0];
			node->fw_ver[0] = response->payload.raw[1];
			node->fw_ver[1] = response->payload.raw[2];
			node->channel_count = response->payload.raw[3];
			node->last_seen_ms = HAL_GetTick();

			if (response->data_len > SERVICE_F001_FIRST_UID_OFFSET) {
				ServiceManager_CopyUidBytes(
						(uint8_t)node_index,
						0U,
						&response->payload.raw[SERVICE_F001_FIRST_UID_OFFSET],
						(uint8_t)(response->data_len - SERVICE_F001_FIRST_UID_OFFSET));
			}
			break;

		case SERVICE_F001_FRAME_UID_MIDDLE:
			ServiceManager_CopyUidBytes(
					(uint8_t)node_index,
					SERVICE_F001_SECOND_UID_OFFSET,
					response->payload.raw,
					response->data_len);
			break;

		case SERVICE_F001_FRAME_UID_TAIL:
			ServiceManager_CopyUidBytes(
					(uint8_t)node_index,
					SERVICE_F001_THIRD_UID_OFFSET,
					response->payload.raw,
					response->data_len);
			break;

		default:
			return false;
	}

	runtime->f001_data_count++;
	return true;
}

/*
 * DONE handler для F001 GET_DEVICE_INFO.
 * Вызывается ServiceManager_HandleDone(); после успешного F001 запускает F004.
 */
static void ServiceManager_HandleGetInfoDone(const CAN_Response_t* response)
{
	if (response == NULL) {
		return;
	}

	int node_index = ServiceManager_FindNodeIndex(response->source_addr);
	if (node_index < 0) {
		ServiceManager_LogNodeError(
				response->source_addr,
				CAN_CMD_SRV_GET_INFO,
				"DONE without DATA");
		return;
	}

	ServiceNodeRuntime_t* runtime = &g_node_runtime[node_index];

	if (runtime->stage == SERVICE_NODE_STAGE_ERROR) {
		return;
	}

	if (runtime->f001_data_count < SERVICE_F001_REQUIRED_DATA_FRAMES) {
		ServiceManager_MarkNodeError(
				(uint8_t)node_index,
				CAN_CMD_SRV_GET_INFO,
				"incomplete F001 DATA");
		return;
	}

	ServiceManager_ApplyUidBytes((uint8_t)node_index, g_inventory[node_index].uid);
	g_inventory[node_index].last_seen_ms = HAL_GetTick();
	runtime->stage = SERVICE_NODE_STAGE_INFO_DONE;
	runtime->active_operation_id = SERVICE_MANAGER_OPERATION_NONE;

	(void)ServiceManager_StartNodeServiceCommand(
			(uint8_t)node_index,
			CAN_CMD_SRV_GET_UID);
}

/*
 * DATA handler для F004 GET_UID.
 * Вызывается ServiceManager_HandleData() после transaction validation.
 */
static bool ServiceManager_HandleGetUidData(const CAN_Response_t* response)
{
	if (response == NULL) {
		return false;
	}

	int node_index = ServiceManager_FindNodeIndex(response->source_addr);
	if (node_index < 0) {
		return false;
	}

	ServiceNodeRuntime_t* runtime = &g_node_runtime[node_index];

	if (runtime->f004_data_count >= SERVICE_F004_REQUIRED_DATA_FRAMES) {
		ServiceManager_MarkNodeError(
				(uint8_t)node_index,
				CAN_CMD_SRV_GET_UID,
				"extra F004 DATA");
		return false;
	}

	ServiceManager_CopyUidBytes(
			(uint8_t)node_index,
			(uint8_t)(runtime->f004_data_count * SERVICE_CAN_DATA_PAYLOAD_BYTES),
			response->payload.raw,
			response->data_len);

	runtime->f004_data_count++;
	return true;
}

/*
 * DONE handler для F004 GET_UID.
 * Вызывается ServiceManager_HandleDone(); сверяет UID и запускает F007.
 */
static void ServiceManager_HandleGetUidDone(const CAN_Response_t* response)
{
	if (response == NULL) {
		return;
	}

	int node_index = ServiceManager_FindNodeIndex(response->source_addr);
	if (node_index < 0) {
		return;
	}

	ServiceNodeRuntime_t* runtime = &g_node_runtime[node_index];

	if (runtime->f004_data_count < SERVICE_F004_REQUIRED_DATA_FRAMES) {
		ServiceManager_MarkNodeError(
				(uint8_t)node_index,
				CAN_CMD_SRV_GET_UID,
				"incomplete F004 DATA");
		return;
	}

	uint32_t uid[SERVICE_UID_WORD_COUNT];
	ServiceManager_ApplyUidBytes((uint8_t)node_index, uid);

	if (!ServiceManager_UidMatchesNode((uint8_t)node_index, uid)) {
		ServiceManager_MarkNodeError(
				(uint8_t)node_index,
				CAN_CMD_SRV_GET_UID,
				"UID mismatch between F001 and F004");
		return;
	}

	memcpy(g_inventory[node_index].uid, uid, sizeof(uid));
	g_inventory[node_index].last_seen_ms = HAL_GetTick();
	runtime->stage = SERVICE_NODE_STAGE_UID_DONE;
	runtime->active_operation_id = SERVICE_MANAGER_OPERATION_NONE;

	(void)ServiceManager_StartNodeServiceCommand(
			(uint8_t)node_index,
			CAN_CMD_SRV_GET_STATUS);
}

/*
 * Перевод metric_id F007 в индекс массива status_current/status_last.
 * Вызывается ServiceManager_HandleGetStatusData().
 */
static bool ServiceManager_StatusMetricIndex(uint16_t metric_id, uint8_t* out_index)
{
	if (metric_id < SERVICE_STATUS_FIRST_METRIC_ID ||
			metric_id > SERVICE_STATUS_LAST_METRIC_ID) {
		return false;
	}

	if (out_index != NULL) {
		*out_index = (uint8_t)(metric_id - SERVICE_STATUS_FIRST_METRIC_ID);
	}

	return true;
}

/*
 * Текстовое имя common F007 metric для диагностического лога.
 * Вызывается ServiceManager_HandleGetStatusDone().
 */
static const char* ServiceManager_StatusMetricName(uint16_t metric_id)
{
	switch (metric_id) {
		case SERVICE_STATUS_RX_TOTAL:
			return "rx_total";
		case SERVICE_STATUS_TX_TOTAL:
			return "tx_total";
		case SERVICE_STATUS_RX_QUEUE_OVERFLOW:
			return "rx_queue_overflow";
		case SERVICE_STATUS_TX_QUEUE_OVERFLOW:
			return "tx_queue_overflow";
		case SERVICE_STATUS_DISPATCHER_OVERFLOW:
			return "dispatcher_overflow";
		case SERVICE_STATUS_DROP_NOT_EXT:
			return "drop_not_ext";
		case SERVICE_STATUS_DROP_WRONG_DST:
			return "drop_wrong_dst";
		case SERVICE_STATUS_DROP_WRONG_TYPE:
			return "drop_wrong_type";
		case SERVICE_STATUS_DROP_WRONG_DLC:
			return "drop_wrong_dlc";
		case SERVICE_STATUS_TX_MAILBOX_TIMEOUT:
			return "tx_mailbox_timeout";
		case SERVICE_STATUS_TX_HAL_ERROR:
			return "tx_hal_error";
		case SERVICE_STATUS_ERROR_CALLBACK:
			return "error_callback";
		case SERVICE_STATUS_ERROR_WARNING:
			return "error_warning";
		case SERVICE_STATUS_ERROR_PASSIVE:
			return "error_passive";
		case SERVICE_STATUS_BUS_OFF:
			return "bus_off";
		case SERVICE_STATUS_LAST_HAL_ERROR:
			return "last_hal_error";
		case SERVICE_STATUS_LAST_ESR:
			return "last_esr";
		case SERVICE_STATUS_APP_QUEUE_OVERFLOW:
			return "app_queue_overflow";
		default:
			return "unknown";
	}
}

/*
 * Отбор fault/drop/overflow counters для delta warning.
 * Вызывается ServiceManager_HandleGetStatusDone().
 */
static bool ServiceManager_StatusMetricIsFaultCounter(uint16_t metric_id)
{
	return (metric_id >= SERVICE_STATUS_RX_QUEUE_OVERFLOW &&
			metric_id <= SERVICE_STATUS_BUS_OFF) ||
			metric_id == SERVICE_STATUS_APP_QUEUE_OVERFLOW;
}

/*
 * DATA handler для F007 GET_STATUS.
 * Вызывается ServiceManager_HandleData() после transaction validation.
 */
static bool ServiceManager_HandleGetStatusData(const CAN_Response_t* response)
{
	if (response == NULL || response->data_len < SERVICE_CAN_DATA_PAYLOAD_BYTES) {
		return false;
	}

	int node_index = ServiceManager_FindNodeIndex(response->source_addr);
	if (node_index < 0) {
		return false;
	}

	uint16_t metric_id = ServiceManager_ReadU16Le(&response->payload.raw[0]);
	uint8_t metric_index = 0U;

	if (!ServiceManager_StatusMetricIndex(metric_id, &metric_index)) {
		ServiceManager_MarkNodeError(
				(uint8_t)node_index,
				CAN_CMD_SRV_GET_STATUS,
				"unknown F007 metric");
		return false;
	}

	g_node_runtime[node_index].status_current[metric_index] =
			ServiceManager_ReadU32Le(&response->payload.raw[2]);
	g_node_runtime[node_index].status_seen_mask |= (1UL << metric_index);

	return true;
}

/*
 * DONE handler для F007 GET_STATUS.
 * Вызывается ServiceManager_HandleDone(); закрывает service chain в READY
 * или фиксирует diagnostic delta относительно предыдущего baseline.
 */
static void ServiceManager_HandleGetStatusDone(const CAN_Response_t* response)
{
	if (response == NULL) {
		return;
	}

	int node_index = ServiceManager_FindNodeIndex(response->source_addr);
	if (node_index < 0) {
		return;
	}

	ServiceNodeRuntime_t* runtime = &g_node_runtime[node_index];
	bool critical_fault_seen = false;

	if ((runtime->status_seen_mask & SERVICE_STATUS_REQUIRED_MASK) !=
			SERVICE_STATUS_REQUIRED_MASK) {
		ServiceManager_MarkNodeError(
				(uint8_t)node_index,
				CAN_CMD_SRV_GET_STATUS,
				"incomplete F007 metrics");
		return;
	}

	if (!runtime->status_baseline_valid) {
		memcpy(runtime->status_last,
		       runtime->status_current,
		       sizeof(runtime->status_last));
		runtime->status_baseline_valid = true;

		char msg[APP_LOG_MESSAGE_MAX_LEN];
		snprintf(msg, sizeof(msg),
				"INFO: Service diagnostics baseline captured: node=0x%02X.",
				g_inventory[node_index].node_id);
		ServiceManager_LogText(msg);
	}
	else {
		for (uint8_t i = 0; i < SERVICE_STATUS_METRIC_COUNT; i++) {
			uint16_t metric_id = (uint16_t)(SERVICE_STATUS_FIRST_METRIC_ID + i);
			uint32_t delta = runtime->status_current[i] - runtime->status_last[i];

			if (delta > 0U && ServiceManager_StatusMetricIsFaultCounter(metric_id)) {
				char msg[APP_LOG_MESSAGE_MAX_LEN];
				snprintf(msg, sizeof(msg),
						"WARNING: Service diagnostics delta: node=0x%02X metric=%s delta=%lu.",
						g_inventory[node_index].node_id,
						ServiceManager_StatusMetricName(metric_id),
						(unsigned long)delta);
				ServiceManager_LogText(msg);

				/*
				 * F007 DONE closes service diagnostics for this node.
				 * bus_off means the executor saw a critical CAN bus fault;
				 * the Conductor must stop accepting normal Host operations.
				 */
				if (metric_id == SERVICE_STATUS_BUS_OFF) {
					critical_fault_seen = true;
					SetSystemError(HOST_ERR_HARDWARE);
				}
			}
		}

		memcpy(runtime->status_last,
		       runtime->status_current,
		       sizeof(runtime->status_last));
	}

	if (critical_fault_seen) {
		g_inventory[node_index].is_online = false;
		runtime->stage = SERVICE_NODE_STAGE_ERROR;
		runtime->active_operation_id = SERVICE_MANAGER_OPERATION_NONE;

		if (g_recovery_active &&
				g_recovery_target_node_id == g_inventory[node_index].node_id) {
			g_recovery_active = false;
			g_recovery_target_node_id = CAN_ADDR_BROADCAST;
		}
		return;
	}

	g_inventory[node_index].is_online = true;
	g_inventory[node_index].last_seen_ms = HAL_GetTick();
	runtime->stage = SERVICE_NODE_STAGE_READY;
	runtime->active_operation_id = SERVICE_MANAGER_OPERATION_NONE;
}

/*
 * Service ACK dispatcher.
 * Вызывается ServiceManager_Run(); F001 ACK не идет в transaction table,
 * F004/F007 ACK валидируются общей executor transaction state machine.
 */
static void ServiceManager_HandleAck(const CanRoutedResponse_t* routed)
{
	if (routed->context_command_code == CAN_CMD_SRV_GET_INFO) {
		return;
	}

	ExecutorTransactionUpdate_t update =
			ExecutorTransactionTable_HandleAck(&g_service_transactions, routed);

	if (!update.matched) {
		ServiceManager_LogIgnoredResponse(
				routed,
				update.reason != NULL ? update.reason : "ACK without transaction");
	}
}

/*
 * Service NACK dispatcher.
 * Вызывается ServiceManager_Run(); переводит конкретный node в ERROR.
 */
static void ServiceManager_HandleNack(const CanRoutedResponse_t* routed)
{
	const CAN_Response_t* response = &routed->parsed;

	if (routed->context_command_code == CAN_CMD_SRV_GET_INFO) {
		int node_index = ServiceManager_EnsureNodeIndex(response->source_addr);
		if (node_index >= 0) {
			ServiceManager_MarkNodeError(
					(uint8_t)node_index,
					CAN_CMD_SRV_GET_INFO,
					"F001 NACK");
		}
		return;
	}

	ExecutorTransactionUpdate_t update =
			ExecutorTransactionTable_HandleNack(&g_service_transactions, routed);

	int node_index = ServiceManager_FindNodeIndex(response->source_addr);
	if (update.matched && node_index >= 0) {
		ServiceManager_MarkNodeError(
				(uint8_t)node_index,
				update.low_command_code,
				"service NACK");
	}
	else {
		ServiceManager_LogIgnoredResponse(
				routed,
				update.reason != NULL ? update.reason : "NACK without transaction");
	}
}

/*
 * Общая validation-прокладка для DATA F004/F007.
 * Вызывается ServiceManager_HandleData() перед command-specific parser-ом.
 */
static bool ServiceManager_ValidateTransactionData(const CanRoutedResponse_t* routed)
{
	ExecutorTransactionUpdate_t update =
			ExecutorTransactionTable_HandleData(&g_service_transactions, routed);

	if (!update.matched ||
			update.event == EXECUTOR_TRANSACTION_EVENT_PROTOCOL_ERROR) {
		int node_index = ServiceManager_FindNodeIndex(routed->parsed.source_addr);
		if (node_index >= 0) {
			ServiceManager_MarkNodeError(
					(uint8_t)node_index,
					routed->context_command_code,
					update.reason != NULL ? update.reason : "DATA protocol error");
		}
		else {
			ServiceManager_LogIgnoredResponse(routed, "DATA without node");
		}
		return false;
	}

	return true;
}

/*
 * Общая validation-прокладка для DONE F004/F007.
 * Вызывается ServiceManager_HandleDone() перед command-specific finalizer-ом.
 */
static bool ServiceManager_ValidateTransactionDone(const CanRoutedResponse_t* routed)
{
	ExecutorTransactionUpdate_t update =
			ExecutorTransactionTable_HandleDone(&g_service_transactions, routed);

	if (!update.matched ||
			update.event == EXECUTOR_TRANSACTION_EVENT_PROTOCOL_ERROR) {
		int node_index = ServiceManager_FindNodeIndex(routed->parsed.source_addr);
		if (node_index >= 0) {
			ServiceManager_MarkNodeError(
					(uint8_t)node_index,
					routed->context_command_code,
					update.reason != NULL ? update.reason : "DONE protocol error");
		}
		else {
			ServiceManager_LogIgnoredResponse(routed, "DONE without node");
		}
		return false;
	}

	return true;
}

/*
 * DATA dispatcher для service command-specific handlers.
 * Вызывается ServiceManager_Run() после проверки subtype DATA.
 */
static void ServiceManager_HandleData(const CanRoutedResponse_t* routed)
{
	const CAN_Response_t* response = &routed->parsed;

	switch (routed->context_command_code) {
		case CAN_CMD_SRV_GET_INFO:
			if (!ServiceManager_HandleGetInfoData(response)) {
				ServiceManager_LogIgnoredResponse(routed, "invalid F001 DATA");
			}
			break;

		case CAN_CMD_SRV_GET_UID:
			if (ServiceManager_ValidateTransactionData(routed)) {
				(void)ServiceManager_HandleGetUidData(response);
			}
			break;

		case CAN_CMD_SRV_GET_STATUS:
			if (ServiceManager_ValidateTransactionData(routed)) {
				(void)ServiceManager_HandleGetStatusData(response);
			}
			break;

		default:
			ServiceManager_LogIgnoredResponse(routed, "unknown service DATA");
			break;
	}
}

/*
 * DONE dispatcher для service command-specific handlers.
 * Вызывается ServiceManager_Run() после проверки subtype DONE.
 */
static void ServiceManager_HandleDone(const CanRoutedResponse_t* routed)
{
	switch (routed->context_command_code) {
		case CAN_CMD_SRV_GET_INFO:
			ServiceManager_HandleGetInfoDone(&routed->parsed);
			break;

		case CAN_CMD_SRV_GET_UID:
			if (ServiceManager_ValidateTransactionDone(routed)) {
				ServiceManager_HandleGetUidDone(&routed->parsed);
			}
			break;

		case CAN_CMD_SRV_GET_STATUS:
			if (ServiceManager_ValidateTransactionDone(routed)) {
				ServiceManager_HandleGetStatusDone(&routed->parsed);
			}
			break;

		default:
			ServiceManager_LogIgnoredResponse(routed, "unknown service DONE");
			break;
	}
}

/*
 * Public init.
 * Вызывается startup path task_dispatcher до discovery и первого Host job.
 */
void ServiceManager_Init(void)
{
	memset(g_inventory, 0, sizeof(g_inventory));
	memset(g_node_runtime, 0, sizeof(g_node_runtime));
	g_nodes_count = 0U;
	g_discovery_window_active = false;
	g_discovery_started_ms = 0U;
	g_discovery_operation_id = SERVICE_MANAGER_OPERATION_NONE;
	g_next_service_operation_id = SERVICE_MANAGER_FIRST_OPERATION_ID;
	g_late_service_response_count = 0U;
	g_recovery_active = false;
	g_recovery_target_node_id = CAN_ADDR_BROADCAST;
	ExecutorTransactionTable_Reset(&g_service_transactions);
}

/*
 * Общий запуск discovery.
 * force_restart используется только recovery path после executor timeout.
 */
static void ServiceManager_StartDiscoveryInternal(bool force_restart)
{
	if (force_restart) {
		ServiceManager_ResetActiveServiceOperations();
	}
	else if (g_discovery_window_active || ServiceManager_HasActiveNodeOperations()) {
		return;
	}

	CAN_Message_t msg;

	for (uint8_t i = 0; i < MAX_DISCOVERED_NODES; i++) {
		g_inventory[i].is_online = false;
		ServiceManager_ResetNodeRuntime(i);
	}

	g_discovery_operation_id = ServiceManager_NextOperationId();
	g_discovery_started_ms = HAL_GetTick();
	g_discovery_window_active = true;

	Packer_CreateGetInfoMsg(CAN_ADDR_BROADCAST, &msg);

	if (!ServiceManager_SubmitServiceCommand(
			&msg,
			CAN_CMD_SRV_GET_INFO,
			CAN_ADDR_BROADCAST,
			g_discovery_operation_id)) {
		ServiceManager_CloseDiscoveryWindow();
	}
}

/**
 * @brief Запуск широковещательного сканирования шины.
 * Вызывается startup path task_dispatcher; повторный вызов игнорируется,
 * если service chain еще активен.
 */
void ServiceManager_StartDiscovery(void)
{
	ServiceManager_StartDiscoveryInternal(false);
}

void ServiceManager_StartRecovery(uint8_t target_node_id)
{
	if (target_node_id == CAN_ADDR_BROADCAST ||
			target_node_id == CAN_ADDR_CONDUCTOR) {
		SetSystemError(HOST_ERR_HARDWARE);
		return;
	}

	char msg[APP_LOG_MESSAGE_MAX_LEN];
	snprintf(msg, sizeof(msg),
			"WARNING: Service recovery started after executor timeout: node=0x%02X.",
			target_node_id);
	ServiceManager_LogText(msg);

	g_recovery_active = true;
	g_recovery_target_node_id = target_node_id;
	SetSystemBusy();
	ServiceManager_StartDiscoveryInternal(true);
}

/**
 * @brief Обновление данных об узле при получении ответа DATA (0xF001)
 * Legacy public API. Сейчас основной F001 path идет через
 * ServiceManager_HandleGetInfoData(); функция оставлена для внешнего контракта
 * и не переводит узел в online/READY.
 */
void ServiceManager_UpdateNode(const CAN_Response_t* res)
{
	if (res == NULL || res->command_code != CAN_CMD_SRV_GET_INFO) {
		return;
	}

	int node_index = ServiceManager_EnsureNodeIndex(res->source_addr);
	if (node_index < 0 || res->data_len < SERVICE_F001_MIN_FIRST_DATA_BYTES) {
		return;
	}

	DeviceNode_t* node = &g_inventory[node_index];
	node->device_type = res->payload.raw[0];
	node->fw_ver[0] = res->payload.raw[1];
	node->fw_ver[1] = res->payload.raw[2];
	node->channel_count = res->payload.raw[3];
	node->last_seen_ms = HAL_GetTick();
}

/**
 * @brief Проверка READY физических узлов согласно маске INIT (0x1002)
 * Вызывается HostRecipeOperation_Start() для INIT preflight.
 */
bool ServiceManager_CheckInventory(uint8_t modules_mask)
{
	uint32_t required_nodes_mask = DeviceMapping_GetRequiredNodesMask(modules_mask);

	for (uint8_t i = 0; i < 32U; i++) {
		if ((required_nodes_mask & (1UL << i)) != 0U) {
			uint8_t target_node = (uint8_t)(0x20U + i);

			if (!ServiceManager_IsNodeOnline(target_node)) {
				return false;
			}
		}
	}

	return true;
}

/**
 * @brief Проверка, что конкретный узел прошел service chain до READY.
 * Вызывается ServiceManager_CheckInventory().
 */
bool ServiceManager_IsNodeOnline(uint8_t node_id)
{
	for (uint8_t i = 0; i < g_nodes_count; i++) {
		if (g_inventory[i].node_id == node_id &&
				g_inventory[i].is_online &&
				g_node_runtime[i].stage == SERVICE_NODE_STAGE_READY) {
			return true;
		}
	}

	return false;
}

/*
 * Главный polling loop service layer.
 * Вызывается task_jobs_monitor после CanResponseRouter_Run() и до JobManager_Run().
 */
void ServiceManager_Run(void)
{
	ServiceManager_CheckDiscoveryWindowTimeout();
	ServiceManager_HandleTransactionTimeouts();
	ServiceManager_CheckRecoveryComplete();

	CanRoutedResponse_t routed;

	while (xQueueReceive(can_service_rx_queue_handle, &routed, 0) == pdPASS) {
		if (!ServiceManager_ResponseIsCurrent(&routed)) {
			ServiceManager_LogIgnoredResponse(&routed, "stale service context");
			continue;
		}

		const CAN_Response_t* response = &routed.parsed;

		switch (response->msg_type) {
			case CAN_MSG_TYPE_ACK:
				ServiceManager_HandleAck(&routed);
				break;

			case CAN_MSG_TYPE_NACK:
				ServiceManager_HandleNack(&routed);
				break;

			case CAN_MSG_TYPE_DATA_DONE_LOG:
				if (response->sub_type == CAN_SUB_TYPE_DATA) {
					ServiceManager_HandleData(&routed);
				}
				else if (response->sub_type == CAN_SUB_TYPE_DONE) {
					ServiceManager_HandleDone(&routed);
				}
				else {
					ServiceManager_LogIgnoredResponse(&routed, "unsupported service subtype");
				}
				break;

			default:
				ServiceManager_LogIgnoredResponse(&routed, "unsupported service message type");
				break;
		}
	}

	ServiceManager_CheckDiscoveryWindowTimeout();
	ServiceManager_HandleTransactionTimeouts();
	ServiceManager_CheckRecoveryComplete();
}
