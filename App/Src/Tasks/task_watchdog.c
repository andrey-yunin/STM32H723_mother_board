/*
 * task_watchdog.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#include "task_watchdog.h"
#include "task_logger.h"
#include "task_dispatcher.h"
#include "Dispatcher/dispatcher_io.h"
#include "cmsis_os.h"
#include "main.h"
#include "shared_resources.h"
#include "app_config.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define WATCHDOG_STARTUP_GRACE_MS          3000U
#define WATCHDOG_CRITICAL_MISSED_LIMIT     3U

// --- Определение массива heartbeat-счётчиков ---
volatile uint32_t task_heartbeats[TASK_COUNT] = {0};

extern IWDG_HandleTypeDef hiwdg1;

// --- Имена задач для логирования ---
static const char *task_names[TASK_COUNT] = {
		"CAN_HANDLER",
        "USB_HANDLER",
        "DISPATCHER",
        "JOBS_MONITOR",
        "LOGGER"
  };

static const bool task_is_critical[TASK_COUNT] = {
		true,  /* CAN_HANDLER */
		true,  /* USB_HANDLER */
		true,  /* DISPATCHER */
		true,  /* JOBS_MONITOR */
		false  /* LOGGER: non-critical text diagnostics path */
};

static void Watchdog_LogTaskStall(uint32_t task_id, uint32_t missed_count)
{
	char log_message[APP_LOG_MESSAGE_MAX_LEN];
	const char* severity = task_is_critical[task_id] ? "ERROR" : "WARNING";

	snprintf(log_message, sizeof(log_message),
			"%s: Task %s heartbeat stalled (%lu missed checks).",
			severity,
			task_names[task_id],
			(unsigned long)missed_count);
	(void)Logger_LogText(log_message);
}

static void Watchdog_RaiseRuntimeFault(uint32_t task_id)
{
	char log_message[APP_LOG_MESSAGE_MAX_LEN];

	snprintf(log_message, sizeof(log_message),
			"ERROR: Runtime watchdog fault latched by task %s.",
			task_names[task_id]);
	(void)Logger_LogText(log_message);

	SetSystemError(HOST_ERR_HARDWARE);
}

static void Watchdog_RefreshHardware(void)
{
	(void)HAL_IWDG_Refresh(&hiwdg1);
}

void app_start_task_watchdog(void *argument)
{
	static uint32_t prev_heartbeats[TASK_COUNT] = {0};
	static uint32_t missed_counts[TASK_COUNT] = {0};
	bool runtime_fault_latched = false;

	(void)argument;

	// Первый цикл — дать задачам время на инициализацию
	osDelay(pdMS_TO_TICKS(WATCHDOG_STARTUP_GRACE_MS));

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
				if (missed_counts[i] < UINT32_MAX) {
					missed_counts[i]++;
				}

				if (missed_counts[i] == WATCHDOG_CRITICAL_MISSED_LIMIT) {
					Watchdog_LogTaskStall((uint32_t)i, missed_counts[i]);
				}

				if (task_is_critical[i] &&
						missed_counts[i] >= WATCHDOG_CRITICAL_MISSED_LIMIT &&
						!runtime_fault_latched) {
					Watchdog_RaiseRuntimeFault((uint32_t)i);
					runtime_fault_latched = true;
				}
				}
			else {
				missed_counts[i] = 0U;
			}
			prev_heartbeats[i] = current;
			}
		if (!runtime_fault_latched) {
			Watchdog_RefreshHardware();
		}
		}
}
