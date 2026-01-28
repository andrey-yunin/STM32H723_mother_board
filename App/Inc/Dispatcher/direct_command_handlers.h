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

// Здесь будут добавляться прототипы для других прямых команд

#endif /* INC_DISPATCHER_DIRECT_COMMAND_HANDLERS_H_ */
