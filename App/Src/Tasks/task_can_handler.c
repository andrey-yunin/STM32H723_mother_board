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

// --- Внешние переменные ---
// Объявлены в main.c, здесь мы сообщаем компилятору, что будем их использовать.
extern FDCAN_HandleTypeDef hfdcan1;

void app_start_task_can_handler(void *argument)
{
	// --- 1. Инициализация и запуск CAN-шины ---
	// Эти функции активируют CAN-контроллер и разрешают прерывания.
	if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK)
		{// Если CAN не запустился, уходим в вечный цикл, что-то не так с конфигурацией.
		 // В реальной системе здесь будет логирование ошибки.
		while(1);
		 }
	// Активируем уведомления. Например, о новых сообщениях в RX FIFO.
	// Пока что входящие сообщения нам не важны, но для будущего это нужно.
	if (HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK)
		{
		while(1);
		}


	// >>> НАЧАЛО НОВОГО ТЕСТОВОГО КОДА ДЛЯ ДАТЧИКОВ ТЕМПЕРАТУРЫ <<<

	{
		osDelay(1000);

		CanMessage_t test_msg;

		// --- Заполняем заголовок (Header) ---
		test_msg.Header.IdType = FDCAN_STANDARD_ID;
		// Формируем ID: 0x100 | ID исполнителя=2 | ID датчика=0
		test_msg.Header.Identifier = 0x100 | (2 << 4) | 0;
		test_msg.Header.TxFrameType = FDCAN_DATA_FRAME;
		test_msg.Header.DataLength = FDCAN_DLC_BYTES_5; // 1 байт команда + 4 байта payload (индекс датчика)
		// --- Заполняем данные (Data) ---
		test_msg.Data[0] = CMD_GET_TEMPERATURE;
		// В payload передаем индекс датчика, который нас интересует (в данном случае, датчик 0)
		test_msg.Data[1] = 0; // Индекс 0
		test_msg.Data[2] = 0;
		test_msg.Data[3] = 0;
		test_msg.Data[4] = 0;
		// --- Отправляем сообщение в очередь ---
		if (can_tx_queue_handle != NULL)
			{
			xQueueSend(can_tx_queue_handle, &test_msg, 0);
			}
		}
	// <<< КОНЕЦ НОВОГО ТЕСТОВОГО КОДА >>>



/*

	// --- 2. ВРЕМЕННЫЙ КОД ДЛЯ ТЕСТИРОВАНИЯ ИСПОЛНИТЕЛЯ НАСОСОВ И КЛАПАНОВ ---

	{
		osDelay(1000); // Немного увеличим задержку для надежности

		CanMessage_t test_msg;

		// --- Заполняем заголовок (Header) ---
		test_msg.Header.IdType = FDCAN_STANDARD_ID;

		// Формируем ID: 0x100 | ID исполнителя=1 | ID насоса=0 (PUMP_1)
		test_msg.Header.Identifier = 0x100 | (1 << 4) | 0;
		test_msg.Header.TxFrameType = FDCAN_DATA_FRAME;
		test_msg.Header.DataLength = FDCAN_DLC_BYTES_5; // 1 байт команда + 4 байта payload
		// --- Заполняем данные (Data) ---
		// Байт 0: Наша команда.
		test_msg.Data[0] = CMD_SET_PUMP_STATE;
		// Байты 1-4: Наш payload. Отправляем "1", что означает "включить".
		test_msg.Data[1] = 1;
		test_msg.Data[2] = 0;
		test_msg.Data[3] = 0;
		test_msg.Data[4] = 0;
		// --- Отправляем сообщение в очередь ---
		if (can_tx_queue_handle != NULL)
			{
			xQueueSend(can_tx_queue_handle, &test_msg, 0);
			}
		}

	// --- КОНЕЦ ВРЕМЕННОГО КОДА ДЛЯ ТЕСТА ---

*
*
*
*/





	/*
	// --- 2. ВРЕМЕННЫЙ КОД ДЛЯ ПЕРВОГО ФИЗИЧЕСКОГО ТЕСТА ШАГОВЫХ ДВИГАТЕЛЕЙ---
	// Этот блок выполнится один раз при старте задачи.
	// Мы создаем и отправляем команду CMD_ENABLE_MOTOR, чтобы проверить всю цепочку связи.
	{
		// Даем небольшую задержку, чтобы другие задачи успели инициализироваться.
		osDelay(500);

		CanMessage_t test_msg; // Создаем экземпляр нашего сообщения.
		// --- Заполняем заголовок (Header) ---
		// Используем стандартный 11-битный ID.
		test_msg.Header.IdType = FDCAN_STANDARD_ID;
		// Формируем ID по нашему протоколу: 0x100 (команда) | ID исполнителя | ID мотора
		// Для теста отправляем команду исполнителю с ID = 0 и мотору с ID = 0.
		test_msg.Header.Identifier = 0x100 | (0 << 4) | 0;
		// Тип кадра - DATA (не remote, не error).
		test_msg.Header.TxFrameType = FDCAN_DATA_FRAME;
		// Устанавливаем длину данных. 1 байт (команда) + 4 байта (payload) = 5 байт.
		test_msg.Header.DataLength = FDCAN_DLC_BYTES_5;
		// --- Заполняем данные (Data) ---
		// Байт 0: Наша команда.
		test_msg.Data[0] = CMD_ENABLE_MOTOR;
		// Байты 1-4: Наш payload. Отправляем "1", что означает "включить".
		// Важно: отправляем в формате Little Endian.
		test_msg.Data[1] = 1;
		test_msg.Data[2] = 0;
		test_msg.Data[3] = 0;
		test_msg.Data[4] = 0;

		// --- Отправляем сообщение в очередь ---
		// `can_tx_queue_handle` - это наша очередь на отправку.
		// `&test_msg` - указатель на сообщение.
		// `0` - не ждать, если очередь полна (для нашего теста это неважно).
		//xQueueSend(can_tx_queue_handle, &test_msg, 0);

		BaseType_t q_status = xQueueSend(can_tx_queue_handle, &test_msg, 0);

		// >>> ДОБАВЛЕННЫЙ КОД ДЛЯ ТЕСТИРОВАНИЯ ОЧЕРЕДИ <<<
		// Если сообщение успешно помещено в очередь, моргнем светодиодом на Дирижере.
		// Это подтверждает, что наш код формирования сообщения и постановки в очередь работает.
		if (q_status == pdPASS) {
			HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
			// Добавим небольшую задержку, чтобы можно было увидеть моргание,
			// если LED очень быстро моргнет и выключится.
			osDelay(100);
			HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin); // Выключаем LED обратно, если он был включен
			}
		// <<< КОНЕЦ ДОБАВЛЕННОГО КОДА ДЛЯ ТЕСТИРОВАНИЯ ОЧЕРЕДИ >>>


	 }
	// --- КОНЕЦ ВРЕМЕННОГО КОДА ДЛЯ ТЕСТА ---
	 *
	 *
	 */
	// --- 3. Основной цикл задачи ---
	// Этот цикл будет вечно работать, обрабатывая исходящие CAN-сообщения.

	CanMessage_t tx_msg; // Переменная для хранения сообщения, извлеченного из очереди.

	for(;;)
		{
		// Ждем, пока в очереди can_tx_queue_handle не появится сообщение.
		// portMAX_DELAY заставляет задачу "спать", пока очередь пуста, не тратя ресурсы процессора.
		if(xQueueReceive(can_tx_queue_handle, &tx_msg, portMAX_DELAY) == pdPASS)
			{
			// Как только мы получили сообщение из очереди, отправляем его в шину.
			// HAL_FDCAN_AddMessageToTxFifoQ - стандартная функция HAL для постановки кадра в очередь на отправку.
			if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &tx_msg.Header, tx_msg.Data) != HAL_OK)
				{
				// Если отправка не удалась, здесь можно добавить логику обработки ошибок.
				// Например, моргнуть светодиодом ошибки или отправить лог.
				}
			}
		}
}
