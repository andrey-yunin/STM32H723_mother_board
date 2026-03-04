/*
 * executor_simulator.c
 *
 *  Created on: Mar 3, 2026
 *      Author: andrey*
 *
 * Имитатор исполнителей — генерирует DONE-ответы для CAN-команд.
 * Для каждой команды, отправленной дирижером, создаёт ответный кадр
 * типа DATA_DONE_LOG / DONE и помещает его в CAN RX очередь.
 *
 */


#include "Dispatcher/executor_simulator.h"
#include "shared_resources.h"
#include <string.h>

void ExecutorSim_ProcessTxMessage(const CAN_Message_t* cmd_msg)
{
	if (cmd_msg == NULL) return;

	// Извлекаем адрес исполнителя (куда отправили команду)
	uint8_t executor_addr = CAN_GET_DST_ADDR(cmd_msg->id);

	// Извлекаем код команды из payload (байты 0-1, Little-Endian)
	uint16_t cmd_code = (uint16_t)(cmd_msg->data[0] | ((uint16_t)cmd_msg->data[1] << 8));

	// --- Формируем DONE-ответ ---
	// Протокол: MSG_TYPE = DATA_DONE_LOG (3)
    // Payload: [0] = CAN_SUB_TYPE_DONE (0x01), [1-2] = cmd_code (LE)
    // Source = executor_addr, Destination = Conductor
    CAN_Message_t done_msg;
    memset(&done_msg, 0, sizeof(done_msg));

    done_msg.id = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
    		CAN_MSG_TYPE_DATA_DONE_LOG,
			CAN_ADDR_CONDUCTOR,
			executor_addr);

    done_msg.is_extended = true;
    done_msg.dlc = 3;
    done_msg.data[0] = CAN_SUB_TYPE_DONE;
    done_msg.data[1] = (uint8_t)(cmd_code & 0xFF);
    done_msg.data[2] = (uint8_t)((cmd_code >> 8) & 0xFF);

    // Помещаем в RX-очередь (там его прочитает JobManager_Run)
    if (can_rx_queue_handle != NULL) {
    	xQueueSend(can_rx_queue_handle, &done_msg, 0);
    	}
}
