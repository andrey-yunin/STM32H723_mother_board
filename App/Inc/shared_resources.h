/*
 * shared_resources.h
 *
 *  Created on: Nov 27, 2025
 *      Author: andrey
 */

#ifndef INC_SHARED_RESOURCES_H_
#define INC_SHARED_RESOURCES_H_

#include "FreeRTOS.h"
#include "queue.h"
#include "cmsis_os.h"


// Объявления очередей
extern QueueHandle_t usb_rx_queue_handle;
extern QueueHandle_t usb_tx_queue_handle;
extern QueueHandle_t can_rx_queue_handle;
extern QueueHandle_t can_tx_queue_handle;
extern QueueHandle_t log_queue_handle;

// Объявление семафоров

extern osSemaphoreId_t usb_tx_semHandle;



#endif /* INC_SHARED_RESOURCES_H_ */
