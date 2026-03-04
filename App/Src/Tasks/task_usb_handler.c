/*
 * task_usb_handler.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#include "Dispatcher/dispatcher_io.h"
#include"task_usb_handler.h"
#include "cmsis_os.h"
#include "shared_resources.h"
#include "app_config.h"
#include "usbd_cdc_if.h" // Для функции CDC_Transmit_HS
#include "string.h"      // Для strlen
#include "usb_device.h"
#include "main.h"
#include "task_watchdog.h"

void app_start_task_usb_handler(void *argument)
{
	MX_USB_DEVICE_Init();
	osDelay(200); // Даем USB время на инициализацию

	USB_TxPacket_t received_packet;

	for(;;)
		{
		// 1. Ждем пакет из очереди на отправку
		Watchdog_Kick(TASK_ID_USB_HANDLER);
		if (xQueueReceive(usb_tx_queue_handle, &received_packet, pdMS_TO_TICKS(1000)) == pdPASS)
			{
			// 2.  Ждём освобождения USB TX с таймаутом.
			// Если callback CDC_TransmitCplt_HS не сработал за 1 сек —
			// пропускаем пакет, чтобы не зависнуть навсегда.
			if (osSemaphoreAcquire(usb_tx_semHandle, pdMS_TO_TICKS(1000)) != osOK)
				{
				// USB TX завис — принудительно освобождаем семафор для следующего пакета
				osSemaphoreRelease(usb_tx_semHandle);
				continue;
				}

			// 3. Отправляем данные по USB, используя точную длину из пакета
			uint16_t len_to_send = received_packet.length;

			// Важно: мы не добавляем \r\n, т.к. это было бы неправильно для бинарных данных.
			// Если для строковых сообщений это будет необходимо,
			// мы добавим это в `Dispatcher_SendUsbResponse`.

			// Пытаемся отправить с лимитом попыток.
			// Если USB отключён или хост не принимает данные — не зависаем.
			int retries = 200;
			while (CDC_Transmit_HS(received_packet.data, len_to_send) == USBD_BUSY)
				{
				if (--retries <= 0)
					{
					break;  // USB недоступен — пропускаем пакет
					}
				osDelay(1);
				}
			}
		}
}


