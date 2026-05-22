/*
 * direct_command_handlers.c
 *
 *  Created on: Jan 21, 2026
 *      Author: andrey
 */

#include "direct_command_handlers.h"
#include "dispatcher_io.h"
#include "host_direct_operation.h"
#include "safety_operation.h"
#include "task_dispatcher.h"

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

#define HOST_ANALYZER_STATUS_OFF        0U
#define HOST_ANALYZER_STATUS_READY      1U
#define HOST_ANALYZER_STATUS_BUSY       2U
#define HOST_ANALYZER_STATUS_ERROR      3U

static uint8_t handle_get_status_host_state(SystemState_t state)
{
	switch (state) {
		case SYS_STATE_READY:
			return HOST_ANALYZER_STATUS_READY;

		case SYS_STATE_INITIALIZING:
		case SYS_STATE_BUSY:
			return HOST_ANALYZER_STATUS_BUSY;

		case SYS_STATE_ERROR:
			return HOST_ANALYZER_STATUS_ERROR;

		case SYS_STATE_POWER_ON:
		default:
			return HOST_ANALYZER_STATUS_OFF;
	}
}

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
	// 2nd & 3rd bytes: Last error code

	uint16_t last_error = GetSystemErrorCode();
	uint8_t data_payload[3];
	data_payload[0] = handle_get_status_host_state(current_state);
	data_payload[1] = (uint8_t)(last_error >> 8);
	data_payload[2] = (uint8_t)(last_error & 0xFFU);

    // Send the DATA response
	Dispatcher_SendData(command_code, HOST_RESPONSE_TYPE_DATA, HOST_STATUS_OK, data_payload, sizeof(data_payload));

	// Complete the command with a DONE response
	Dispatcher_SendDone(command_code, HOST_STATUS_OK);

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

	Dispatcher_SendData(cmd_code, HOST_RESPONSE_TYPE_DATA, HOST_STATUS_OK, ver_data, sizeof(ver_data));
	Dispatcher_SendDone(cmd_code, HOST_STATUS_OK);
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

	Dispatcher_SendData(cmd_code, HOST_RESPONSE_TYPE_DATA, HOST_STATUS_OK, dt_data, sizeof(dt_data));
	Dispatcher_SendDone(cmd_code, HOST_STATUS_OK);
}


void handle_emergency_stop(uint16_t cmd_code, const uint8_t* params, uint16_t len)
{
	(void)params;
	(void)len;

	SafetyOperation_Start(cmd_code);
}

void handle_thermo_get_temp(uint16_t cmd_code, const uint8_t* params, uint16_t len)
{
	HostDirectOperation_StartThermoGetTemp(cmd_code, params, len);
}


void handle_sensor_get_all_temps(uint16_t cmd_code, const uint8_t* params, uint16_t len)
{
	HostDirectOperation_StartSensorGetAllTemps(cmd_code, params, len);
}

void handle_sensor_get_temp(uint16_t cmd_code, const uint8_t* params, uint16_t len)
{
	HostDirectOperation_StartSensorGetTemp(cmd_code, params, len);
}
