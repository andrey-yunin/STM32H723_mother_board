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

void app_start_task_can_handler(void *argument)
{
	// --- 1. Инициализация и запуск CAN-шины ---
	// Эти функции активируют CAN-контроллер и разрешают прерывания.

	/*
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

	*/

	// >>> ТЕСТОВЫЙ КОД ОТКЛЮЧЁН (тип CanMessage_t несовместим с CAN_Message_t в очереди) <<<





	#if 0

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

#endif


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

	CAN_Message_t tx_msg;  // Абстрактное CAN-сообщение из очереди

	for(;;)
		{
		Watchdog_Kick(TASK_ID_CAN_HANDLER);
		if(xQueueReceive(can_tx_queue_handle, &tx_msg, pdMS_TO_TICKS(1000)) == pdPASS)
			{

			// Конвертация CAN_Message_t → HAL FDCAN формат
			// --- Режим имитатора: HAL-отправка пропущена ---
			// Когда подключён реальный CAN-трансивер, раскомментировать
			// блок конвертации и отправки HAL_FDCAN_AddMessageToTxFifoQ.
			/*

			FDCAN_TxHeaderTypeDef hal_header;
			hal_header.Identifier = tx_msg.id;
			hal_header.IdType = tx_msg.is_extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
	        hal_header.TxFrameType = FDCAN_DATA_FRAME;
	        hal_header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	        hal_header.BitRateSwitch = FDCAN_BRS_OFF;
	        hal_header.FDFormat = FDCAN_CLASSIC_CAN;
	        hal_header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	        hal_header.MessageMarker = 0;

	        // Преобразование DLC: число байт → константа HAL
	        switch (tx_msg.dlc) {
	        		case 0: hal_header.DataLength = FDCAN_DLC_BYTES_0; break;
	                case 1: hal_header.DataLength = FDCAN_DLC_BYTES_1; break;
	                case 2: hal_header.DataLength = FDCAN_DLC_BYTES_2; break;
	                case 3: hal_header.DataLength = FDCAN_DLC_BYTES_3; break;
	                case 4: hal_header.DataLength = FDCAN_DLC_BYTES_4; break;
	                case 5: hal_header.DataLength = FDCAN_DLC_BYTES_5; break;
	                case 6: hal_header.DataLength = FDCAN_DLC_BYTES_6; break;
	                case 7: hal_header.DataLength = FDCAN_DLC_BYTES_7; break;
	                default: hal_header.DataLength = FDCAN_DLC_BYTES_8; break;
	                }

	        if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &hal_header, tx_msg.data) != HAL_OK)
	        	{
	        	// TODO: обработка ошибки отправки
	        	}
	        	*/

	        // Имитатор: генерируем DONE-ответ для каждой отправленной команды.
	        // Убрать эту строку при подключении реального CAN-оборудования.
	        ExecutorSim_ProcessTxMessage(&tx_msg);
	        }
		}
}
