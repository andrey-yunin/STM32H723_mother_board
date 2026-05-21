/*
 * task_logger.h
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#ifndef INC_TASKS_TASK_LOGGER_H_
#define INC_TASKS_TASK_LOGGER_H_

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"

typedef struct {
	char text[APP_LOG_MESSAGE_MAX_LEN];
	uint16_t length;
} LoggerMessage_t;

void app_start_task_logger(void *argument);
bool Logger_LogText(const char* message);

#endif /* INC_TASKS_TASK_LOGGER_H_ */
