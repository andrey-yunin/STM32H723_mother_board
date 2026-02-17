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
    		 uint8_t received_dispenser_id = raw_params[0];
    		 uint16_t received_volume = read_uint16_from_buffer(&raw_params[1]);
    		 uint16_t received_cycles = raw_params[3];

    		 cmd->args.dispenser_wash.dispenser_id = received_dispenser_id;
    		 cmd->args.dispenser_wash.pump_duration_ms = ParamTranslator_VolumeToPumpDurationMs(received_dispenser_id, received_volume);
    		 cmd->args.dispenser_wash.cycles = received_cycles;
             // NEW: Call the dedicated wash-specific translator functions
             cmd->args.dispenser_wash.rotate_steps = ParamTranslator_DispenserWashRotateSteps();
             cmd->args.dispenser_wash.steps_down = ParamTranslator_DispenserWashZToStepsDown();
             cmd->args.dispenser_wash.steps_up = ParamTranslator_DispenserWashZToStepsUp();


    		 cmd->args_type = ARGS_TYPE_PARSED; // Устанавливаем флаг ТОЛЬКО ЗДЕСЬ при успехе
    		 }
    	 else {
    		 success = false; // Неправильная длина параметров
    		 }
    	 break;

     case 0x4000: // Команда  <-- added 05/02/2026
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

    case 0x2100: // DISPENSER_ASPIRATE command
    	if (params_len == 6) {
    		uint8_t received_dispenser_id = raw_params[0];
    		uint8_t received_source_type = raw_params[1];
    		uint16_t received_position = read_uint16_from_buffer(&raw_params[2]);
    		uint16_t received_volume = read_uint16_from_buffer(&raw_params[4]);

    		cmd->args.dispenser_aspirate.dispenser_id = received_dispenser_id;
    		cmd->args.dispenser_aspirate.source_type = received_source_type;

    		// Вызов функций транслятора для получения низкоуровневых параметров
    		cmd->args.dispenser_aspirate.rotate_steps = ParamTranslator_DispenserSlotToRotateSteps(received_source_type, received_position);
    		cmd->args.dispenser_aspirate.steps_down = ParamTranslator_DispenserZToStepsDown(received_source_type, received_position);
    		cmd->args.dispenser_aspirate.steps_up = ParamTranslator_DispenserZToStepsUp(received_source_type, received_position);
    		cmd->args.dispenser_aspirate.pump_duration_ms = ParamTranslator_VolumeToPumpDurationMs(received_dispenser_id, received_volume);

    		cmd->args_type = ARGS_TYPE_PARSED;
    		}
    	else {
    		success = false;
    		}
    	break;

    case 0x2200: // DISPENSER_DISPENSE command added 13/02/2026
    	if (params_len == 6) {
    		uint8_t received_dispenser_id = raw_params[0];
    		uint8_t received_target_type = raw_params[1];
    		uint16_t received_position = read_uint16_from_buffer(&raw_params[2]);
    		uint16_t received_volume = read_uint16_from_buffer(&raw_params[4]);

    		cmd->args.dispenser_dispense.dispenser_id = received_dispenser_id;
    		cmd->args.dispenser_dispense.target_type = received_target_type;

    		// Вызов функций транслятора для получения низкоуровневых параметров
    		cmd->args.dispenser_dispense.rotate_steps = ParamTranslator_DispenserTargetToRotateSteps(received_target_type, received_position);
    		cmd->args.dispenser_dispense.steps_down = ParamTranslator_DispenserTargetZToStepsDown(received_target_type, received_position);
    		cmd->args.dispenser_dispense.steps_up = ParamTranslator_DispenserTargetZToStepsUp(received_target_type, received_position);
    		cmd->args.dispenser_dispense.pump_duration_ms = ParamTranslator_VolumeToPumpDurationMs(received_dispenser_id, received_volume);

    		cmd->args_type = ARGS_TYPE_PARSED;
    		}
    	else {
    		success = false;
    	}
    	break;

    case 0x5000: // REAGENT_ROTATE command added 12/02/2026
    	if (params_len == 3) {
    		uint8_t received_rotor_id = raw_params[0];
    		uint16_t received_slot_number = read_uint16_from_buffer(&raw_params[1]);

    		cmd->args.reagent_rotate.rotor_id = received_rotor_id;
    		cmd->args.reagent_rotate.rotate_steps = ParamTranslator_ReagentRotorSlotToSteps(received_rotor_id, received_slot_number);

    		cmd->args_type = ARGS_TYPE_PARSED;
    		}
    	else {
    		success = false;
    	}
    	break;

    case 0x3100: // MIXER_MIX command added 13/02/2026
    	if (params_len == 6) { // Общая длина параметров: mixer_id (1 байт) + cuvette (2 байта) + duration (2 байта) + wash_cycles (1 байт) = 6 байт
    		// Чтение необработанных параметров из буфера:
    		uint8_t received_mixer_id = raw_params[0];                              // mixer_id: первый байт
    		uint16_t received_cuvette = read_uint16_from_buffer(&raw_params[1]);    // cuvette: 2 байта, начиная со смещения 1
    		uint16_t received_duration = read_uint16_from_buffer(&raw_params[3]);   // duration: 2 байта, начиная со смещения 3
    		uint8_t received_wash_cycles = raw_params[5];                           // wash_cycles: 1 байт, по смещению 5

    		// Заполнение структуры ParsedArgs_MixerMix:
    		cmd->args.mixer_mix.mixer_id = received_mixer_id;                       // Сохраняем ID миксера напрямую
    		cmd->args.mixer_mix.rotate_steps = ParamTranslator_MixerCuvetteToRotationSteps(received_cuvette); // Трансляция cuvette в шаги вращения (низкоуровневый параметр)
    		cmd->args.mixer_mix.z_steps_down = ParamTranslator_MixerZToStepsDown(received_mixer_id, received_cuvette); // Расчет шагов для опускания лопатки
    		cmd->args.mixer_mix.z_steps_up = ParamTranslator_MixerZToStepsUp(received_mixer_id, received_cuvette);   // Расчет шагов для подъема лопатки
    		cmd->args.mixer_mix.duration_ms = received_duration;                    // Сохраняем длительность перемешивания (уже в мс, низкоуровневый параметр)
    		cmd->args.mixer_mix.wash_cycles = received_wash_cycles;                 // Сохраняем количество промывок (высокоуровневый параметр для JobManager)

    		cmd->args_type = ARGS_TYPE_PARSED; // Отмечаем, что параметры успешно разобраны
    		}
    	else {
    		success = false; // Неправильная длина параметров
    		}
    	break;


    case 0x6100: // PHOTOMETER_SCAN_SINGLE command added 16/02/2026
    	if (params_len == 3) {
    		uint16_t received_cuvette = read_uint16_from_buffer(&raw_params[0]);
    		uint8_t received_mask = raw_params[2];

    		// Вызов транслятора для получения шагов
    		cmd->args.photometer_scan_single.rotate_steps = ParamTranslator_PhotometerCuvetteToSteps(received_cuvette);

    		// Прямое сохранение маски
    		cmd->args.photometer_scan_single.wavelength_mask = received_mask;

    		cmd->args_type = ARGS_TYPE_PARSED;
    		}
    	else {
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



