/*
 * direct_command_handlers.c
 *
 *  Created on: Jan 21, 2026
 *      Author: andrey
 */

#include "device_mapping.h"
#include "direct_command_handlers.h"
#include "dispatcher_io.h"
#include "app_init_checker.h" // For GetSystemState
#include "task_dispatcher.h"
#include "can_packer.h"

/**
      * @brief Handler for the direct command GET_STATUS (0x1000)
      * @param command_code The command code
      * @param params Pointer to parameters (not used for this command)
      * @param params_len Length of parameters (not used for this command)
     */
void handle_get_status(uint16_t command_code, const uint8_t* params, uint16_t params_len)
{
	// Get the current system state
	SystemState_t current_state = GetSystemState();
	// Form the payload for the DATA response
	// 1st byte: Current system state
	// 2nd & 3rd bytes: Last error code (0 for now)

	uint8_t data_payload[3];
	data_payload[0] = (uint8_t)current_state;
	data_payload[1] = 0x00; // ErrorCode MSB
	data_payload[2] = 0x00; // ErrorCode LSB

    // Send the DATA response
	Dispatcher_SendData(command_code, 0x03, 0x0000, data_payload, sizeof(data_payload));

	// Complete the command with a DONE response
	Dispatcher_SendDone(command_code, 0x0000);

}

void handle_get_version(uint16_t cmd_code, const uint8_t* params, uint16_t len)
{
	// Формат ответа согласно commands.md: [Major(1)][Minor(1)][Build(2)][Date(8)]
	uint8_t ver_data[12] = {1, 0, 0, 42, '2', '0', '2', '6', '0', '4', '1', '3'};
	Dispatcher_SendData(cmd_code, 0x03, 0x0000, ver_data, 12);
}

void handle_get_datetime(uint16_t cmd_code, const uint8_t* params, uint16_t len)
{
	// Заглушка: 2026-04-13 12:00:00
	uint8_t dt_data[7] = {0x07, 0xEA, 4, 13, 12, 0, 0}; // Year(2), M, D, H, M, S
	Dispatcher_SendData(cmd_code, 0x03, 0x0000, dt_data, 7);
}

void handle_emergency_stop(uint16_t cmd_code, const uint8_t* params, uint16_t len)
{

	CAN_Message_t can_msg;

	// Рассылаем команду STOP на широковещательный адрес 0x00
	Packer_CreateStopMotorMsg(0, &can_msg); // ch_idx 0 (для broadcast игнорируется)
	can_msg.id = CAN_BUILD_ID(CAN_PRIORITY_HIGH, CAN_MSG_TYPE_COMMAND, CAN_ADDR_BROADCAST, CAN_ADDR_CONDUCTOR);
	xQueueSend(can_tx_queue_handle, &can_msg, 0);
	Dispatcher_SendDone(cmd_code, 0x0000);
}

void handle_thermo_get_temp(uint16_t cmd_code, const uint8_t* params, uint16_t len)
{
	uint8_t thermo_id = params[0];

	DevicePhysAddr_t phys = DeviceMapping_GetThermoPhysAddr(thermo_id); // Используем маппинг

	if (phys.is_valid) {
		CAN_Message_t can_msg;
		Packer_CreateGetTempMsg(phys.ch_idx, &can_msg);
		can_msg.id = CAN_BUILD_ID(CAN_PRIORITY_HIGH, CAN_MSG_TYPE_COMMAND, phys.node_id, CAN_ADDR_CONDUCTOR);
		xQueueSend(can_tx_queue_handle, &can_msg, 0);

		// Ответ придет асинхронно через JobManager_Run (тип DATA)
		}
	else
	{
		Dispatcher_SendError(cmd_code, 0x0003); // Invalid Params
		}
}







