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

#define CONDUCTOR_FW_VERSION_MAJOR      1U
#define CONDUCTOR_FW_VERSION_MINOR      0U
#define CONDUCTOR_FW_VERSION_BUILD      42U
#define CONDUCTOR_FW_BUILD_DATE         "2026-04-13"

#define CONDUCTOR_PLACEHOLDER_YEAR      2026U
#define CONDUCTOR_PLACEHOLDER_MONTH     4U
#define CONDUCTOR_PLACEHOLDER_DAY       13U
#define CONDUCTOR_PLACEHOLDER_HOUR      12U
#define CONDUCTOR_PLACEHOLDER_MINUTE    0U
#define CONDUCTOR_PLACEHOLDER_SECOND    0U




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


/*
 * Возвращает версию прошивки Дирижера.
 *
 * Формат Host DATA по full_examples.md:
 * [major:U8][minor:U8][build:U16 BE][date:ASCII "YYYY-MM-DD"].
 * Parser уже отправил ACK; handler отправляет DATA и закрывает команду DONE.
 */




void handle_get_version(uint16_t cmd_code, const uint8_t* params, uint16_t len)
{
	(void)params;
	(void)len;

	uint16_t build = CONDUCTOR_FW_VERSION_BUILD;
	const char build_date[] = CONDUCTOR_FW_BUILD_DATE;

	uint8_t ver_data[14] = {
		(uint8_t)CONDUCTOR_FW_VERSION_MAJOR,
		(uint8_t)CONDUCTOR_FW_VERSION_MINOR,
		(uint8_t)((build >> 8) & 0xFFU),
		(uint8_t)(build & 0xFFU),
		(uint8_t)build_date[0],
		(uint8_t)build_date[1],
		(uint8_t)build_date[2],
		(uint8_t)build_date[3],
		(uint8_t)build_date[4],
		(uint8_t)build_date[5],
		(uint8_t)build_date[6],
		(uint8_t)build_date[7],
		(uint8_t)build_date[8],
		(uint8_t)build_date[9]
	};

	Dispatcher_SendData(cmd_code, 0x03, 0x0000, ver_data, sizeof(ver_data));
	Dispatcher_SendDone(cmd_code, 0x0000);
}

/*
 * Возвращает дату и время Дирижера.
 *
 * Формат Host DATA по commands.md:
 * [year:U16 BE][month:U8][day:U8][hour:U8][minute:U8][second:U8].
 * Parser уже отправил ACK; handler отправляет DATA и закрывает команду DONE.
 */
void handle_get_datetime(uint16_t cmd_code, const uint8_t* params, uint16_t len)
{
	(void)params;
	(void)len;

	uint16_t year = (uint16_t)CONDUCTOR_PLACEHOLDER_YEAR;

	uint8_t dt_data[7] = {
		(uint8_t)((year >> 8) & 0xFFU),
		(uint8_t)(year & 0xFFU),
		(uint8_t)CONDUCTOR_PLACEHOLDER_MONTH,
		(uint8_t)CONDUCTOR_PLACEHOLDER_DAY,
		(uint8_t)CONDUCTOR_PLACEHOLDER_HOUR,
		(uint8_t)CONDUCTOR_PLACEHOLDER_MINUTE,
		(uint8_t)CONDUCTOR_PLACEHOLDER_SECOND
	};

	Dispatcher_SendData(cmd_code, 0x03, 0x0000, dt_data, sizeof(dt_data));
	Dispatcher_SendDone(cmd_code, 0x0000);
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


void handle_sensor_get_all_temps(uint16_t cmd_code, const uint8_t* params, uint16_t len)
{
	(void)params;
	(void)len;

	Dispatcher_SendError(cmd_code, 0x000A);
	Dispatcher_SendUsbResponse("ERROR: SENSOR_GET_ALL_TEMPS is not connected to HOST_DIRECT yet.");
}

void handle_sensor_get_temp(uint16_t cmd_code, const uint8_t* params, uint16_t len)
{
	(void)params;
	(void)len;

	Dispatcher_SendError(cmd_code, 0x000A);
	Dispatcher_SendUsbResponse("ERROR: SENSOR_GET_TEMP is not connected to HOST_DIRECT yet.");
}
