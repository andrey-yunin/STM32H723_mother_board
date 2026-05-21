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
#include <string.h>

#define LOGGER_QUEUE_TIMEOUT_MS 50U

/*
 * Public text logging API.
 *
 * Text diagnostics must enter the logging path through log_queue_handle.
 * Host protocol binary packets use dispatcher_io.c and bypass this function.
 * The current task_logger sink is intentionally stubbed until the CAN log
 * frame format is accepted.
 */
bool Logger_LogText(const char* message)
{
	if (message == NULL || log_queue_handle == NULL) {
		return false;
	}

	LoggerMessage_t log_message;
	uint16_t length = 0U;

	while (message[length] != '\0' &&
			length < (APP_LOG_MESSAGE_MAX_LEN - 1U)) {
		length++;
	}

	memcpy(log_message.text, message, length);
	log_message.text[length] = '\0';
	log_message.length = length;

	return xQueueSend(
			log_queue_handle,
			&log_message,
			pdMS_TO_TICKS(LOGGER_QUEUE_TIMEOUT_MS)) == pdPASS;
}

void app_start_task_logger(void *argument)
{
	LoggerMessage_t log_message;

	for(;;)
		{
		Watchdog_Kick(TASK_ID_LOGGER);

		// Ждём сообщение из log_queue с таймаутом (для heartbeat)
		if (xQueueReceive(log_queue_handle, (void *)&log_message, pdMS_TO_TICKS(1000)) == pdPASS)
			{
			/*
			 * Временная заглушка до утверждения CAN log frame.
			 * Логический log_queue уже отделен от USB-типа; sink будет
			 * подключен здесь после согласования CAN ID/payload.
			 */
			(void)log_message;
			}
		}
}
