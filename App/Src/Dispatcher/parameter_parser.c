/*
 * parameter_parser.c
 *
 *  Created on: Jan 29, 2026
 *      Author: andrey
 */

#include "parameter_parser.h"
#include "command_parser.h"
#include <string.h> // для memcpy
#include "param_translator.h" // added 06/02/2026

/*
// Вспомогательная функция для безопасного чтения 4-байтного числа
static uint32_t read_uint32_from_buffer(const uint8_t* buffer) {
	return ((uint32_t)buffer[0] << 24) |
		   ((uint32_t)buffer[1] << 16) |
		   ((uint32_t)buffer[2] << 8)  |
		   ((uint32_t)buffer[3]);
	}
	*/

// Вспомогательная функция для безопасного чтения 2-байтного числа
static uint16_t read_uint16_from_buffer(const uint8_t* buffer) {
	return ((uint16_t)buffer[0] << 8) | buffer[1];
	}




bool Parameters_Parse(UniversalCommand_t* cmd, const uint8_t* raw_params, uint16_t params_len)
{
	// По умолчанию, если команда не требует разбора, это не ошибка
	bool success = true;

	switch (cmd->command_code)
	{
	case 0x1002: // INIT
		//
		// Ожидаем 1 байт (маска модулей)
		if (params_len == 1) {
			cmd->args.init.modules_mask = raw_params[0];

			cmd->args_type = ARGS_TYPE_PARSED; // Устанавливаем флаг ТОЛЬКО ЗДЕСЬ при успехе
			} else {
				success = false; // Неправильная длина параметров
				}
		break;

     case 0x2000: // DISPENSER_WASH
    	 // Согласно документации: dispenser_id (1) + volume (2) + cycles (1) = 4 байта
    	 if (params_len == 4) {
    		 cmd->args.dispenser_wash.dispenser_id = raw_params[0];

    		 // Читаем 2-байтный volume, начиная с 1-го байта
    		 cmd->args.dispenser_wash.volume = read_uint16_from_buffer(&raw_params[1]);

    		 // Читаем 1-байтный cycles, начиная с 3-го байта
    		 cmd->args.dispenser_wash.cycles = raw_params[3];

    		 cmd->args_type = ARGS_TYPE_PARSED; // Устанавливаем флаг ТОЛЬКО ЗДЕСЬ при успехе
    		 }
    	 else {
    		 success = false; // Неправильная длина параметров
    		 }
    	 break;

     case 0x4000: // Команда WASH_STATION_WASH <-- added 05/02/2026
    	 if (params_len == 3) {
    		 cmd->args.wash_station_wash.cycles = raw_params[0];

    		 // Вычисляем шаги поворота диска, используя функцию из param_translator
    		 cmd->args.wash_station_wash.rotate_steps = ParamTranslator_CuvetteToSteps(read_uint16_from_buffer(&raw_params[1]));
    		 cmd->args_type = ARGS_TYPE_PARSED;
    		 }
    	 else {
    		 success = false;
    		 }
    	 break;

    case 0x5110: // SAMPLE_ROTATE command
    	if (params_len == 2) {
    		uint16_t received_slot = read_uint16_from_buffer(&raw_params[0]);
    		cmd->args.sample_rotate.rotate_steps = ParamTranslator_SampleDiskSlotToSteps(received_slot);
    		cmd->args_type = ARGS_TYPE_PARSED;
    		} else {
    			success = false;
    			}
    	break;




    	 // --- [ADD_NEW_PARSER] ---
		 // Здесь будут добавляться case для других команд

     default:
    	 // Для команд без параметров (например, GET_VERSION) или
    	 // для которых не нужен специальный разбор.
    	 cmd->args_type = ARGS_TYPE_BINARY;
    	 if (params_len > 0) {
    		 memcpy(cmd->args.binary.raw, raw_params, params_len);
    		 }
    	 cmd->args.binary.len = params_len;
    	 break;
		}

	return success;
	}



