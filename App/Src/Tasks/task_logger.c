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
	/** char log_buffer[APP_LOG_MESSAGE_MAX_LEN];


    for(;;)
    	{
    	// Ждем сообщение из очереди log_queue
    	if (xQueueReceive(log_queue_handle, (void *)log_buffer, portMAX_DELAY) == pdPASS)
    		{
    		// Полученное от диспетчера сообщение перенаправляем в очередь на отправку по USB
    		xQueueSend(usb_tx_queue_handle, (void *)log_buffer, pdMS_TO_TICKS(100));
    		}
    	HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    	osDelay(1000); // Задержка 1 секунда
        }
        */

}


