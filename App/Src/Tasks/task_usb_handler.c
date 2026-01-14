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

void app_start_task_usb_handler(void *argument)
{
	MX_USB_DEVICE_Init();
	osDelay(200); // Даем USB время на инициализацию

	USB_TxPacket_t received_packet;

	for(;;)
		{
		// 1. Ждем пакет из очереди на отправку
		if (xQueueReceive(usb_tx_queue_handle, &received_packet, portMAX_DELAY) == pdPASS)
			{
			// 2. Ждем, пока USB-передатчик освободится
			osSemaphoreAcquire(usb_tx_semHandle, osWaitForever);

			// 3. Отправляем данные по USB, используя точную длину из пакета
			uint16_t len_to_send = received_packet.length;

			// Важно: мы не добавляем \r\n, т.к. это было бы неправильно для бинарных данных.
			// Если для строковых сообщений это будет необходимо,
			// мы добавим это в `Dispatcher_SendUsbResponse`.

			// Пытаемся начать передачу, пока USB занят
			while (CDC_Transmit_HS(received_packet.data, len_to_send) == USBD_BUSY)
				{
				osDelay(1); // Уступаем процессорное время
				}
			// Как только CDC_Transmit_HS вернет USBD_OK, передача началась.
			// Семафор будет освобожден в колбэке CDC_TransmitCplt_HS.
			}
		}
}


