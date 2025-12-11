/*
 * can_message.h
 *
 *  Created on: Dec 11, 2025
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_CAN_MESSAGE_H_
#define INC_DISPATCHER_CAN_MESSAGE_H_

#include "main.h" // Для FDCAN_TxHeaderTypeDef

// Эта структура будет использоваться для передачи CAN-сообщений
// через очередь FreeRTOS внутри Дирижера.
typedef struct {
	FDCAN_TxHeaderTypeDef Header; // Заголовок CAN-кадра от HAL
	uint8_t Data[8];  // 8 байт данных CAN-кадра
	}CanMessage_t;

#endif /* INC_DISPATCHER_CAN_MESSAGE_H_ */
