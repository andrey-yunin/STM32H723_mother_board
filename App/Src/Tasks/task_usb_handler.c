/*
 * task_usb_handler.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

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
	  /* init code for USB_DEVICE */

	  MX_USB_DEVICE_Init();

	  // Добавляем мигание светодиодом для визуального подтверждения работы задачи...

	  HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin); // Вкл.
	  osDelay(100);
	  HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin); // Выкл.
	  osDelay(100);


	char tx_buffer[APP_USB_RESP_MAX_LEN];

	for(;;)
		{
		// 1. Ждем сообщение из очереди на отправку

		if (xQueueReceive(usb_tx_queue_handle, (void *)tx_buffer, portMAX_DELAY) == pdPASS)
			{
			//for(int i=0; i<10; i++) { HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin); osDelay(50); }

			// 2.  Сообщение получено в tx_buffer.
			// Эта функция будет "спать", пока семафор не будет поднят в CDC_TransmitCplt_HS.

		//	osSemaphoreAcquire(usb_tx_semHandle, osWaitForever);



			// 3. Теперь USB-передатчик свободен, можем безопасно отправлять
			// Вручную находим конец строки и добавляем \r\n

			size_t len = strlen(tx_buffer);

			// 4. Проверяем, что в буфере есть место для \r и \n

			if (len + 2 < APP_USB_RESP_MAX_LEN) {
				tx_buffer[len] = '\r';
				tx_buffer[len + 1] = '\n';
				len += 2;
				}

			// 5. Отправляем данные по USB

			/*



			while (CDC_Transmit_HS((uint8_t *)tx_buffer, len) == USBD_BUSY)
			{
				osDelay(100);
			}

			*/


			 // Ждем, пока предыдущая передача завершится (семафор будет отпущен в CDC_TransmitCplt_HS)
			 osSemaphoreAcquire(usb_tx_semHandle, osWaitForever);


			 while (CDC_Transmit_HS((uint8_t *)tx_buffer, len) == USBD_BUSY)
				 {
				 // Уступаем процессорное время другим задачам, пока ждем,
				 // что USB освободится.
				 osDelay(1);
				 }
			 // Как только CDC_Transmit_HS вернет USBD_OK, передача началась.
			 // Семафор будет освобожден в колбэке CDC_TransmitCplt_HS.
			 }
		}
}


