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
	char rx_buffer[APP_USB_CMD_MAX_LEN]; // Буфер для команды, полученной по USB
	char confirmation_msg[APP_USB_RESP_MAX_LEN]; // Буфер для сообщения-подтверждения
	//char *anw = "OK I'M GETTING COMMANDS!";


  /* Infinite loop */
  for(;;)
  {
	  // 1. Ждем команду из очереди usb_rx_queue
	  if (xQueueReceive(usb_rx_queue_handle, (void *)rx_buffer, portMAX_DELAY) == pdPASS)
         {
		  // 2. Используем самую простую форму snprintf для создания ответа.
             // Это может снова вызвать предупреждение компилятора, но мы его пока проигнорируем для теста.
             snprintf(confirmation_msg, APP_USB_RESP_MAX_LEN, "DISPATCHER: Received '%s'", rx_buffer);


          // 3. Отправляем подтверждение в очередь на передачу по USB
             xQueueSend(usb_tx_queue_handle, (void *)confirmation_msg, pdMS_TO_TICKS(100));
         }
   }
}
