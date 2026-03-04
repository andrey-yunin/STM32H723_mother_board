/*
 * task_watchdog.h
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#ifndef INC_TASKS_TASK_WATCHDOG_H_
#define INC_TASKS_TASK_WATCHDOG_H_

#include <stdint.h>

// --- ID задач для heartbeat-мониторинга ---
#define TASK_ID_CAN_HANDLER    0
#define TASK_ID_USB_HANDLER    1
#define TASK_ID_DISPATCHER     2
#define TASK_ID_JOBS_MONITOR   3
#define TASK_ID_LOGGER         4
#define TASK_COUNT             5

// --- Период проверки watchdog (мс) ---
#define WATCHDOG_CHECK_INTERVAL_MS  1000

// --- Массив счётчиков heartbeat (определение в task_watchdog.c) ---
// volatile — массив модифицируется из разных задач, компилятор не должен кэшировать значения
extern volatile uint32_t task_heartbeats[TASK_COUNT];

// --- Макрос для отметки "задача жива" ---
#define Watchdog_Kick(task_id)  (task_heartbeats[(task_id)]++)

// --- Точка входа задачи ---
void app_start_task_watchdog(void *argument);

#endif /* INC_TASKS_TASK_WATCHDOG_H_ */
