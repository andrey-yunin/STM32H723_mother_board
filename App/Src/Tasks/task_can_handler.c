/*
 * task_can_handler.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#include "Tasks/task_can_handler.h"
#include "cmsis_os.h"
#include "main.h"               // Для FDCAN_HandleTypeDef
#include "shared_resources.h"   // Для extern объявлений очередей
#include "Dispatcher/can_packer.h"
#include "Dispatcher/dispatcher_io.h"
#include "task_dispatcher.h"
#include "task_logger.h"
#include "task_watchdog.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <stdio.h>

#define TASK_CAN_FAULT_ERROR_WARNING          (1UL << 0)
#define TASK_CAN_FAULT_ERROR_PASSIVE          (1UL << 1)
#define TASK_CAN_FAULT_BUS_OFF                (1UL << 2)
#define TASK_CAN_FAULT_HAL_ERROR              (1UL << 3)
#define TASK_CAN_FAULT_TX_ADD_FAILED          (1UL << 4)
#define TASK_CAN_FAULT_ERROR_LOG_OVERFLOW     (1UL << 5)
#define TASK_CAN_FAULT_PROTOCOL_ERROR         (1UL << 6)

// --- Внешние переменные ---
// Объявлены в main.c, здесь мы сообщаем компилятору, что будем их использовать.
extern FDCAN_HandleTypeDef hfdcan1;

static volatile uint32_t g_fdcan_fault_flags = 0U;
static volatile uint32_t g_fdcan_error_status_its = 0U;
static volatile uint32_t g_fdcan_hal_error_code = 0U;

static uint8_t FDCAN_DlcToBytes(uint32_t data_length)
{
	switch (data_length) {
		case FDCAN_DLC_BYTES_0:  return 0;
		case FDCAN_DLC_BYTES_1:  return 1;
		case FDCAN_DLC_BYTES_2:  return 2;
		case FDCAN_DLC_BYTES_3:  return 3;
		case FDCAN_DLC_BYTES_4:  return 4;
		case FDCAN_DLC_BYTES_5:  return 5;
		case FDCAN_DLC_BYTES_6:  return 6;
		case FDCAN_DLC_BYTES_7:  return 7;
		case FDCAN_DLC_BYTES_8:  return 8;
		default:                return 0;
	}
}

static bool TaskCanHandler_IsValidTxFrame(const CAN_Message_t* msg)
{
	return msg != NULL &&
			msg->is_extended &&
			msg->dlc == CAN_PAYLOAD_SIZE &&
			CAN_GET_MSG_TYPE(msg->id) == CAN_MSG_TYPE_COMMAND &&
			CAN_GET_SRC_ADDR(msg->id) == CAN_ADDR_CONDUCTOR;
}

static void TaskCanHandler_RecordFault(uint32_t fault_flags,
		uint32_t error_status_its,
		uint32_t hal_error_code)
{
	g_fdcan_fault_flags |= fault_flags;
	g_fdcan_error_status_its |= error_status_its;

	if (hal_error_code != HAL_FDCAN_ERROR_NONE) {
		g_fdcan_hal_error_code = hal_error_code;
	}
}

static void TaskCanHandler_ClearHalErrorCode(FDCAN_HandleTypeDef* hfdcan)
{
	if (hfdcan != NULL) {
		hfdcan->ErrorCode = HAL_FDCAN_ERROR_NONE;
	}
}

static void TaskCanHandler_ProcessFaults(void)
{
	uint32_t fault_flags;
	uint32_t error_status_its;
	uint32_t hal_error_code;

	taskENTER_CRITICAL();
	fault_flags = g_fdcan_fault_flags;
	error_status_its = g_fdcan_error_status_its;
	hal_error_code = g_fdcan_hal_error_code;
	g_fdcan_fault_flags = 0U;
	g_fdcan_error_status_its = 0U;
	g_fdcan_hal_error_code = 0U;
	taskEXIT_CRITICAL();

	if (fault_flags == 0U) {
		return;
	}

	FDCAN_ProtocolStatusTypeDef protocol_status;
	FDCAN_ErrorCountersTypeDef error_counters;
	(void)HAL_FDCAN_GetProtocolStatus(&hfdcan1, &protocol_status);
	(void)HAL_FDCAN_GetErrorCounters(&hfdcan1, &error_counters);

	bool terminal_fault =
			((fault_flags & TASK_CAN_FAULT_BUS_OFF) != 0U) ||
			((fault_flags & TASK_CAN_FAULT_TX_ADD_FAILED) != 0U) ||
			(protocol_status.BusOff != 0U);

	char log_message[APP_LOG_MESSAGE_MAX_LEN];
	snprintf(log_message, sizeof(log_message),
			"%s: FDCAN fault flags=0x%lX status=0x%lX hal=0x%lX bo=%lu ep=%lu ew=%lu.",
			terminal_fault ? "ERROR" : "WARNING",
			(unsigned long)fault_flags,
			(unsigned long)error_status_its,
			(unsigned long)hal_error_code,
			(unsigned long)protocol_status.BusOff,
			(unsigned long)protocol_status.ErrorPassive,
			(unsigned long)protocol_status.Warning);
	(void)Logger_LogText(log_message);

	snprintf(log_message, sizeof(log_message),
			"INFO: FDCAN counters tx=%lu rx=%lu log=%lu.",
			(unsigned long)error_counters.TxErrorCnt,
			(unsigned long)error_counters.RxErrorCnt,
			(unsigned long)error_counters.ErrorLogging);
	(void)Logger_LogText(log_message);

	if (terminal_fault) {
		SetSystemError(HOST_ERR_HARDWARE);
	}
}


/**
 * @brief CALLBACK: Обработка входящих сообщений (Вызывается из прерывания)
 */

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
	if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0) {
		FDCAN_RxHeaderTypeDef rx_header;
		CAN_Message_t rx_msg;

		// 1. Читаем сообщение из аппаратного FIFO 0
		if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rx_header, rx_msg.data) == HAL_OK) {

			// 2. Упаковка во внутренний формат Дирижера
			rx_msg.id          = rx_header.Identifier;
			rx_msg.is_extended = (rx_header.IdType == FDCAN_EXTENDED_ID);
			rx_msg.dlc         = FDCAN_DlcToBytes(rx_header.DataLength);

			// 3. Быстрая отправка в очередь Диспетчера
			BaseType_t xHigherPriorityTaskWoken = pdFALSE;

			if (can_rx_queue_handle != NULL) {
				xQueueSendFromISR(can_rx_queue_handle, &rx_msg, &xHigherPriorityTaskWoken);
				portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
			}
		}
	}
}


void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t ErrorStatusITs)
{
	if (hfdcan == NULL || hfdcan->Instance != FDCAN1) {
		return;
	}

	uint32_t fault_flags = 0U;

	if ((ErrorStatusITs & FDCAN_IT_ERROR_WARNING) != 0U) {
		fault_flags |= TASK_CAN_FAULT_ERROR_WARNING;
	}

	if ((ErrorStatusITs & FDCAN_IT_ERROR_PASSIVE) != 0U) {
		fault_flags |= TASK_CAN_FAULT_ERROR_PASSIVE;
	}

	if ((ErrorStatusITs & FDCAN_IT_BUS_OFF) != 0U) {
		fault_flags |= TASK_CAN_FAULT_BUS_OFF;
	}

	TaskCanHandler_RecordFault(fault_flags, ErrorStatusITs, HAL_FDCAN_ERROR_NONE);
}

void HAL_FDCAN_ErrorCallback(FDCAN_HandleTypeDef *hfdcan)
{
	if (hfdcan == NULL || hfdcan->Instance != FDCAN1) {
		return;
	}

	uint32_t hal_error_code = HAL_FDCAN_GetError(hfdcan);
	if (hal_error_code == HAL_FDCAN_ERROR_NONE) {
		return;
	}

	uint32_t fault_flags = TASK_CAN_FAULT_HAL_ERROR;

	if ((hal_error_code & FDCAN_IT_ERROR_LOGGING_OVERFLOW) != 0U) {
		fault_flags |= TASK_CAN_FAULT_ERROR_LOG_OVERFLOW;
	}

	if ((hal_error_code & (FDCAN_IT_ARB_PROTOCOL_ERROR |
			FDCAN_IT_DATA_PROTOCOL_ERROR)) != 0U) {
		fault_flags |= TASK_CAN_FAULT_PROTOCOL_ERROR;
	}

	TaskCanHandler_RecordFault(fault_flags, 0U, hal_error_code);
	TaskCanHandler_ClearHalErrorCode(hfdcan);
}

void app_start_task_can_handler(void *argument)
{
	(void)argument;

	// --- 1. Инициализация и запуск CAN-шины ---
	// Эти функции активируют CAN-контроллер и разрешают прерывания.

	if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK) {
		// Если CAN не запустился, уходим в вечный цикл, что-то не так с конфигурацией.
		Error_Handler();
	}

	// 2. Активация уведомлений о новых входящих сообщениях и fault-состояниях
	if (HAL_FDCAN_ActivateNotification(&hfdcan1,
			FDCAN_IT_RX_FIFO0_NEW_MESSAGE |
			FDCAN_IT_ERROR_WARNING |
			FDCAN_IT_ERROR_PASSIVE |
			FDCAN_IT_BUS_OFF |
			FDCAN_IT_ERROR_LOGGING_OVERFLOW |
			FDCAN_IT_ARB_PROTOCOL_ERROR |
			FDCAN_IT_DATA_PROTOCOL_ERROR,
			0) != HAL_OK) {
		Error_Handler();
	}

	CAN_Message_t tx_msg;   // Контейнер для исходящего сообщения

	for(;;)
		{
		Watchdog_Kick(TASK_ID_CAN_HANDLER);
		TaskCanHandler_ProcessFaults();

		if (xQueueReceive(can_tx_queue_handle, &tx_msg, pdMS_TO_TICKS(1000)) == pdPASS) {
			if (!TaskCanHandler_IsValidTxFrame(&tx_msg)) {
				continue;
			}

			FDCAN_TxHeaderTypeDef hal_header;

			// Заполнение заголовка HAL согласно Директиве 2.0

			hal_header.Identifier          = tx_msg.id;
			hal_header.IdType              = FDCAN_EXTENDED_ID;
			hal_header.TxFrameType         = FDCAN_DATA_FRAME;
			hal_header.DataLength          = FDCAN_DLC_BYTES_8; // Всегда 8 байт для команд
			hal_header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
			hal_header.BitRateSwitch       = FDCAN_BRS_OFF;
			hal_header.FDFormat            = FDCAN_CLASSIC_CAN;
			hal_header.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
			hal_header.MessageMarker       = 0;

			// 4. Физическая отправка в TxFifo
			if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &hal_header, tx_msg.data) != HAL_OK) {
				// Ошибка постановки в очередь отправки (например, переполнение шины)
				TaskCanHandler_RecordFault(
						TASK_CAN_FAULT_TX_ADD_FAILED,
						0U,
						HAL_FDCAN_GetError(&hfdcan1));
				TaskCanHandler_ClearHalErrorCode(&hfdcan1);
			}
		}
	}
}
