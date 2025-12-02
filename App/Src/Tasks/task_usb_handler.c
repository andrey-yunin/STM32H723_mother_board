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

void app_start_task_usb_handler(void *argument)
{

  char tx_buffer[APP_USB_RESP_MAX_LEN];
  /* Infinite loop */
  for(;;)
  {



	  if (xQueueReceive(usb_tx_queue_handle, (void *)tx_buffer, portMAX_DELAY) == pdPASS)
	  {
		  // Сообщение получено в tx_buffer.
		  // Вручную находим конец строки и добавляем \r\n

		  size_t len = strlen(tx_buffer);

		  // Проверяем, что в буфере есть место для \r и \n

             if (len + 2 < APP_USB_RESP_MAX_LEN) {
            	 tx_buffer[len] = '\r';
            	 tx_buffer[len + 1] = '\n';
                 len += 2;
             }

             while (CDC_Transmit_HS((uint8_t *)tx_buffer, len) == USBD_BUSY)
             {
                 osDelay(1);
             }

             // Небольшая пауза после отправки, чтобы дать хосту и драйверу
             // время обработать пакет как отдельную сущность.
             osDelay(10); // 10 миллисекунд более чем достаточно

	  }
  }
}


