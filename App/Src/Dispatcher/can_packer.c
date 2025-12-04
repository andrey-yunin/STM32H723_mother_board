/*
 * can_packer.c
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#include "Dispatcher/can_packer.h"
#include <string.h> // Для memset
#include <stdbool.h>



void Packer_CreateRotateMotorMsg(uint8_t motor_id, int32_t steps, uint16_t speed, uint32_t job_id, CAN_Message_t* out_msg)
{
	// Заглушка: Просто очищаем сообщение
	memset(out_msg, 0, sizeof(CAN_Message_t));
    // В реальной реализации здесь будет формирование ID и байтов данных
    // out_msg->id = (uint32_t)motor_id << 24 | (CAN_CMD_ROTATE_MOTOR << 16) | (job_id & 0xFFFF);
    // out_msg->data[0] = (uint8_t)(steps >> 24);
    // и так далее...
}

void Packer_CreateStartPumpMsg(uint8_t pump_id, uint32_t job_id, CAN_Message_t* out_msg)
{
	memset(out_msg, 0, sizeof(CAN_Message_t));
}

void Packer_CreateStopPumpMsg(uint8_t pump_id, uint32_t job_id, CAN_Message_t* out_msg)
{
	memset(out_msg, 0, sizeof(CAN_Message_t));
}

void Packer_CreateHomeMotorMsg(uint8_t motor_id, uint16_t speed, uint32_t job_id, CAN_Message_t* out_msg)
{
	memset(out_msg, 0, sizeof(CAN_Message_t));
}

// --- Заглушка для функции-распаковщика ---

bool Packer_ParseCanResponse(const CAN_Message_t* in_msg, CAN_Response_t* out_response)
{
	// Заглушка: Пока всегда возвращаем "успех" для тестовых целей
    if (out_response != NULL) {
    	out_response->job_id = 0; // Пример
        out_response->executor_id = 0; // Пример
        out_response->status_ok = true;
        }
   return true;
}
