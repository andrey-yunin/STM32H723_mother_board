/*
 * task_dispatcher.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#include"task_dispatcher.h"
#include "cmsis_os.h"
#include "shared_resources.h"
#include "app_config.h"
#include "string.h"
#include "stdio.h"
#include  "main.h"

void app_start_task_dispatcher(void *argument)
{
	char rx_buffer[APP_USB_CMD_MAX_LEN];
	char *confirmation_msg = "OK";

  /* Infinite loop */
  for(;;)
  {
	  // 1. Ждем команду из очереди usb_rx_queue
         if (xQueueReceive(usb_rx_queue_handle, (void *)rx_buffer, portMAX_DELAY) == pdPASS)
        	 {

      // 2. Команда получена. В ответ просто отправляем "OK".
             xQueueSend(usb_tx_queue_handle, (void *)confirmation_msg, pdMS_TO_TICKS(100));

              }
  }
}
