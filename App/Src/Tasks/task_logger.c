/*
 * task_logger.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#include "task_logger.h"
#include "cmsis_os.h"
#include "shared_resources.h"
#include "app_config.h"
#include "main.h"


void app_start_task_logger(void *argument)
{

  const char* tick_message = "Tick...";


  /* Infinite loop */
  for(;;)
  {
	  // Отправляем сообщение "Tick..." в очередь usb_tx_queue
     xQueueSend(usb_tx_queue_handle, (void *)tick_message, 0);

	  HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
	  osDelay(1000); // Задержка 1 секунда
  }

}


