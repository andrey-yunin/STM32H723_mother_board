/*
 * app_init_checker.c
 *
 *  Created on: Nov 27, 2025
 *      Author: andrey
 */

#include "app_init_checker.h"
#include "app_safety.h"
#include "shared_resources.h" // Для доступа к глобальным ручкам очередей

extern osThreadId_t task_can_handleHandle;
extern osThreadId_t task_usb_handleHandle;
extern osThreadId_t task_dispatcherHandle;
extern osThreadId_t task_watchdogHandle;
extern osThreadId_t task_jobs_monitHandle;
extern osThreadId_t task_loggerHandle;

/**
 * @brief Проверяет RTOS queues/stream buffer/semaphore после их создания в main().
 *        В случае сбоя уводит систему в Error_Handler до запуска scheduler.
 */
void app_init_checker_verifyqueues(void)
{
	if (usb_tx_semHandle == NULL ||
			usb_rx_queue_handle == NULL ||
			usb_rx_stream_buffer_handle == NULL ||
			usb_tx_queue_handle == NULL ||
			can_rx_queue_handle == NULL ||
			can_job_rx_queue_handle == NULL ||
			can_service_rx_queue_handle == NULL ||
			can_tx_queue_handle == NULL ||
			log_queue_handle == NULL)
	{
		AppSafety_RaiseFatal(APP_FATAL_REASON_STARTUP_RTOS_OBJECTS);
	}
}

/**
 * @brief Проверяет task handles после osThreadNew() в main().
 *        Частично созданная RTOS-система не должна принимать Host-команды.
 */
void app_init_checker_verifytasks(void)
{
	if (task_can_handleHandle == NULL ||
			task_usb_handleHandle == NULL ||
			task_dispatcherHandle == NULL ||
			task_watchdogHandle == NULL ||
			task_jobs_monitHandle == NULL ||
			task_loggerHandle == NULL)
	{
		AppSafety_RaiseFatal(APP_FATAL_REASON_STARTUP_TASK_HANDLES);
	}
}
