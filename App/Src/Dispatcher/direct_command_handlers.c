/*
 * direct_command_handlers.c
 *
 *  Created on: Jan 21, 2026
 *      Author: andrey
 */

#include "direct_command_handlers.h"
#include "dispatcher_io.h"
#include "app_init_checker.h" // For GetSystemState
#include "task_dispatcher.h"

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







