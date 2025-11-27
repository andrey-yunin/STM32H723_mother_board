/*
 * app_config.h
 *
 *  Created on: Nov 27, 2025
 *      Author: andrey
 */

#ifndef INC_APP_CONFIG_H_
#define INC_APP_CONFIG_H_

// --- Queue & Message Buffer Sizes ---
#define APP_USB_RX_QUEUE_LENGTH        10   // Количество элементов в очереди USB RX
#define APP_USB_TX_QUEUE_LENGTH        10   // Количество элементов в очереди USB TX
#define APP_CAN_RX_QUEUE_LENGTH        20   // Количество элементов в очереди CAN RX
#define APP_CAN_TX_QUEUE_LENGTH        20   // Количество элементов в очереди CAN TX
#define APP_LOG_QUEUE_LENGTH           30   // Количество элементов в очереди Логгера

#define APP_USB_CMD_MAX_LEN            256  // Максимальная длина строки команды от ПК (включая null-терминатор)
#define APP_USB_RESP_MAX_LEN           256  // Максимальная длина строки ответа на ПК (включая null-терминатор)
#define APP_CAN_MESSAGE_MAX_LEN         8   // Максимальная длина полезной нагрузки CAN-сообщения (8 байт)
#define APP_LOG_MESSAGE_MAX_LEN        128  // Максимальная длина сообщения для Логгера (включая null-терминатор)

// --- Job Manager Configuration ---
#define APP_MAX_ACTIVE_JOBS            5    // Максимальное количество одновременно активных "проектов"
#define APP_JOB_TIMEOUT_MS             5000 // Тайм-аут для шага "проекта" в миллисекундах (5 секунд)

// --- Task Stack Sizes (in words, CubeMX генерирует * 4 байта) ---
// Эти значения задаются в CubeMX, но могут быть переопределены или использованы здесь для ясности
// osThreadAttr_t task_can_handle_attributes = { .stack_size = 256 * 4, ... };
// #define APP_TASK_CAN_HANDLER_STACK_SIZE    256 // WORDS
// #define APP_TASK_USB_HANDLER_STACK_SIZE    256
// #define APP_TASK_DISPATCHER_STACK_SIZE     512
// #define APP_TASK_WATCHDOG_STACK_SIZE       128
// #define APP_TASK_JOBS_MONITOR_STACK_SIZE   128
// #define APP_TASK_LOGGER_STACK_SIZE         256

// --- Task Priorities (CMSIS-OS v2) ---
// Эти значения задаются в CubeMX, но здесь можно привести для справки
// #define APP_TASK_CAN_HANDLER_PRIORITY      osPriorityHigh1
// #define APP_TASK_USB_HANDLER_PRIORITY      osPriorityHigh2
// #define APP_TASK_DISPATCHER_PRIORITY       osPriorityBelowNormal
// #define APP_TASK_WATCHDOG_PRIORITY         osPriorityHigh
// #define APP_TASK_JOBS_MONITOR_PRIORITY     osPriorityLow
// #define APP_TASK_LOGGER_PRIORITY           osPriorityLow1

#endif /* INC_APP_CONFIG_H_ */
