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
#define APP_CAN_RX_QUEUE_LENGTH        20   // Raw CAN RX frames from task_can_handler
#define APP_CAN_JOB_RX_QUEUE_LENGTH    20   // Routed executor responses for Host operations
#define APP_CAN_SERVICE_RX_QUEUE_LENGTH 20  // Routed executor responses for service/recovery/diagnostics
#define APP_CAN_TX_QUEUE_LENGTH        20   // Количество элементов в очереди CAN TX
#define APP_LOG_QUEUE_LENGTH           30   // Количество элементов в очереди Логгера

#define APP_USB_CMD_MAX_LEN            256  // Максимальная длина строки команды от ПК (включая null-терминатор)
#define APP_USB_RESP_MAX_LEN           256  // Максимальная длина строки ответа на ПК (включая null-терминатор)


//added 03/03/2026 APP_CAN_MESSAGE_MAX_LEN удалён — размер элемента CAN-очередей
// определяется через sizeof(CAN_Message_t) в main.c
// #define APP_CAN_MESSAGE_MAX_LEN         8   // Максимальная длина полезной нагрузки CAN-сообщения (8 байт)
#define APP_LOG_MESSAGE_MAX_LEN        128  // Максимальная длина сообщения для Логгера (включая null-терминатор)

// --- Job Manager Configuration ---
#define MAX_CONCURRENT_JOBS            1     // Один Host job; увеличение требует resource arbitration/recovery policy.
#define JOB_TIMEOUT_MS                 5000  // Базовый тайм-аут шага, если action не задал свой operation timeout.
#define JOB_PUMP_DURATION_MARGIN_MS    1000  // Запас к duration_ms для finite-команд Fluidics.
#define JOB_MOTION_ROTATE_MARGIN_MS    1000  // Запас к расчетному времени finite Motion ROTATE.
#define JOB_MOTION_HOME_TIMEOUT_MS     30000 // Отдельный timeout для HOME-профиля Motion.

// --- Executor Transaction Configuration ---
// ACK должен приходить быстро: исполнитель подтверждает только прием команды.
#define JOB_ACK_TIMEOUT_MS              50U

// Максимум low-level транзакций в одном шаге recipe.
// DONE закрывается по node + command + channel; ACK/NACK трактуются как
// command-level responses, потому что текущий executor-протокол не несет
// channel в ACK/NACK.
#define JOB_MAX_EXECUTOR_TRANSACTIONS   8U


// Максимальный размер бинарных параметров для одной команды
#define MAX_BINARY_ARGS_SIZE 64

// Максимум внутренних actions в одном recipe step.
// Сейчас поддержан только ACTION_WAIT_MS.
#define JOB_MAX_INTERNAL_ACTIONS 8U
#define JOB_INTERNAL_ACTION_TIMEOUT_MARGIN_MS 10U



#endif /* INC_APP_CONFIG_H_ */
