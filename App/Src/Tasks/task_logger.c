/*
 * task_logger.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#include "task_logger.h"
#include "cmsis_os.h"
#include "task_watchdog.h"
#include "shared_resources.h"
#include "app_config.h"
#include "main.h"
#include "dispatcher_io.h"


void app_start_task_logger(void *argument)
{
	USB_TxPacket_t tx_packet;

	for(;;)
		{
		Watchdog_Kick(TASK_ID_LOGGER);

		// Ждём сообщение из log_queue с таймаутом (для heartbeat)
		if (xQueueReceive(log_queue_handle, (void *)&tx_packet, pdMS_TO_TICKS(1000)) == pdPASS)
			{
			// Перенаправляем в очередь USB TX
			xQueueSend(usb_tx_queue_handle, &tx_packet, pdMS_TO_TICKS(100));
			}
		}
}


