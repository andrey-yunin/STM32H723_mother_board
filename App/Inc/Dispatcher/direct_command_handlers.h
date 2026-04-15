/*
 * direct_command_handlers.h
 *
 *  Created on: Jan 21, 2026
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_DIRECT_COMMAND_HANDLERS_H_
#define INC_DISPATCHER_DIRECT_COMMAND_HANDLERS_H_

#include <stdint.h>
#include <stdbool.h> // Для булевых типов, если потребуется
#include "command_parser.h" // Для DirectCommandHandler_t, DirectCommandDescriptor_t

// Прототипы для обработчиков прямых команд
void handle_get_status(uint16_t command_code, const uint8_t* params, uint16_t params_len);
void handle_get_version(uint16_t cmd_code, const uint8_t* params, uint16_t len);
void handle_get_datetime(uint16_t cmd_code, const uint8_t* params, uint16_t len);
void handle_emergency_stop(uint16_t cmd_code, const uint8_t* params, uint16_t len);
void handle_thermo_get_temp(uint16_t cmd_code, const uint8_t* params, uint16_t len);

// Здесь будут добавляться прототипы для других прямых команд

#endif /* INC_DISPATCHER_DIRECT_COMMAND_HANDLERS_H_ */
