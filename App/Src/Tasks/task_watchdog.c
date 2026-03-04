/*
 * task_watchdog.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#include "task_watchdog.h"
#include "cmsis_os.h"
#include "shared_resources.h"
#include "app_config.h"
#include "dispatcher_io.h"
#include <string.h>
#include <stdio.h>

// --- Определение массива heartbeat-счётчиков ---
volatile uint32_t task_heartbeats[TASK_COUNT] = {0};

// --- Имена задач для логирования ---
static const char *task_names[TASK_COUNT] = {
		"CAN_HANDLER",
        "USB_HANDLER",
        "DISPATCHER",
        "JOBS_MONITOR",
        "LOGGER"
  };

void app_start_task_watchdog(void *argument)
{
	static uint32_t prev_heartbeats[TASK_COUNT] = {0};
	USB_TxPacket_t log_packet;

	// Первый цикл — дать задачам время на инициализацию
	osDelay(pdMS_TO_TICKS(3000));

	// Сделать начальный снимок
	for (int i = 0; i < TASK_COUNT; i++)
		{
		prev_heartbeats[i] = task_heartbeats[i];
		}

	for(;;)
		{
		osDelay(pdMS_TO_TICKS(WATCHDOG_CHECK_INTERVAL_MS));
		for (int i = 0; i < TASK_COUNT; i++)
			{
			uint32_t current = task_heartbeats[i];
			if (current == prev_heartbeats[i])
				{
				// Задача не отметилась — возможно зависла
				int len = snprintf((char *)log_packet.data, APP_USB_RESP_MAX_LEN,
						"WARNING: Task %s not responding!", task_names[i]);
				log_packet.length = (uint16_t)len;
				xQueueSend(log_queue_handle, &log_packet, pdMS_TO_TICKS(50));
				}
			prev_heartbeats[i] = current;
			}
		}
}
