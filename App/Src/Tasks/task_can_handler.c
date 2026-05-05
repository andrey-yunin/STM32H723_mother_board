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
#include "can_message.h"        // Для нашей структуры CanMessage_t
#include "command_protocol.h"   // Для перечисления CommandID_t
#include "Dispatcher/can_packer.h"
#include "Dispatcher/executor_simulator.h"
#include "task_watchdog.h"


// --- Внешние переменные ---
// Объявлены в main.c, здесь мы сообщаем компилятору, что будем их использовать.
extern FDCAN_HandleTypeDef hfdcan1;

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
		case FDCAN_DLC_BYTES_8:
		default:                return 8;
	}
}


/**
 * @brief CALLBACK: Обработка входящих сообщений (Вызывается из прерывания)
 */

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
	if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0){
		FDCAN_RxHeaderTypeDef rx_header;
		CAN_Message_t rx_msg;

		// 1. Читаем сообщение из аппаратного FIFO 0
		if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rx_header, rx_msg.data) == HAL_OK){

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


void app_start_task_can_handler(void *argument)
{
	// --- 1. Инициализация и запуск CAN-шины ---
	// Эти функции активируют CAN-контроллер и разрешают прерывания.

	if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK)
		{// Если CAN не запустился, уходим в вечный цикл, что-то не так с конфигурацией.
		Error_Handler();
		}

	 // 2. Активация уведомлений о новых входящих сообщениях (RX FIFO 0)
	if (HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK) {
		Error_Handler();
	}

	CAN_Message_t tx_msg;   // Контейнер для исходящего сообщения

	for(;;)
		{
		Watchdog_Kick(TASK_ID_CAN_HANDLER);
		if(xQueueReceive(can_tx_queue_handle, &tx_msg, pdMS_TO_TICKS(1000)) == pdPASS){
			FDCAN_TxHeaderTypeDef hal_header;

			// Заполнение заголовка HAL согласно Директиве 2.0

			hal_header.Identifier          = tx_msg.id;
			hal_header.IdType              = tx_msg.is_extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
	        hal_header.TxFrameType         = FDCAN_DATA_FRAME;
	        hal_header.DataLength          = FDCAN_DLC_BYTES_8; // Всегда 8 байт для команд
	        hal_header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	        hal_header.BitRateSwitch = FDCAN_BRS_OFF;
	        hal_header.FDFormat = FDCAN_CLASSIC_CAN;
	        hal_header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	        hal_header.MessageMarker = 0;

	        // 4. Физическая отправка в TxFifo
	        if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &hal_header, tx_msg.data) != HAL_OK) {
		        // Ошибка постановки в очередь отправки (например, переполнение шины)
		        // Можно добавить логгер здесь в будущем
	        	}
	        }
		}
	}
