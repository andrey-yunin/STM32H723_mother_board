/*
 * can_packer.c
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 *
 * Updated: Mar 3, 2026 — Phase 8.A: Full CAN frame packing per specification
 */

#include "Dispatcher/can_packer.h"
#include <string.h> // Для memset
#include <stdbool.h>

// ============================================================
// Вспомогательные функции (static)
// ============================================================

// Определяет адрес платы-исполнителя по старшему байту кода команды
static uint8_t Packer_GetExecutorAddr(uint16_t cmd_code)
{
	switch (cmd_code & 0xFF00) {
		case 0x0100: return CAN_ADDR_MOTOR_BOARD;  // 0x01xx — моторы
        case 0x0200: return CAN_ADDR_PUMP_BOARD;    // 0x02xx — насосы
        case 0x0400: return CAN_ADDR_MOTOR_BOARD;   // 0x04xx — фотометр (TODO: назначить отдельный адрес)
        default:     return CAN_ADDR_BROADCAST;
        }
	}

// Упаковка uint16_t в Little-Endian
static void pack_u16_le(uint8_t* dst, uint16_t value)
{
	dst[0] = (uint8_t)(value & 0xFF);
	dst[1] = (uint8_t)((value >> 8) & 0xFF);
  }

// Упаковка int32_t в Little-Endian
static void pack_i32_le(uint8_t* dst, int32_t value)
{
	uint32_t u = (uint32_t)value;
	dst[0] = (uint8_t)(u & 0xFF);
	dst[1] = (uint8_t)((u >> 8) & 0xFF);
	dst[2] = (uint8_t)((u >> 16) & 0xFF);
	dst[3] = (uint8_t)((u >> 24) & 0xFF);
	}

// Распаковка uint16_t из Little-Endian
static uint16_t unpack_u16_le(const uint8_t* src)
{
	return (uint16_t)(src[0] | ((uint16_t)src[1] << 8));
	}

// Инициализация CAN-кадра: очистка, заполнение ID и command code
static void packer_init_msg(CAN_Message_t* msg, uint16_t cmd_code, uint8_t dlc)
{
	memset(msg, 0, sizeof(CAN_Message_t));
	uint8_t dst = Packer_GetExecutorAddr(cmd_code);
	msg->id = CAN_BUILD_ID(CAN_PRIORITY_HIGH, CAN_MSG_TYPE_COMMAND,
			dst, CAN_ADDR_CONDUCTOR);
	msg->dlc = dlc;
	msg->is_extended = true;
	pack_u16_le(&msg->data[0], cmd_code);  // Байты 0-1: код команды (LE)
	}

// ============================================================
// Функции-упаковщики (Conductor → Executor)
// ============================================================

void Packer_CreateRotateMotorMsg(uint8_t motor_id, int32_t steps, uint16_t speed,
		uint32_t job_id, CAN_Message_t* out_msg)
{
	// MOTOR_ROTATE (0x0101): DLC=8
	// [0-1] cmd_code | [2] motor_id | [3-6] steps (LE) | [7] speed (UINT8, /4)
	// speed упаковывается как speed/4 (сдвиг >>2), диапазон 0-1020, шаг 4
	packer_init_msg(out_msg, CAN_CMD_MOTOR_ROTATE, 8);
	out_msg->data[2] = motor_id;
	pack_i32_le(&out_msg->data[3], steps);
	out_msg->data[7] = (uint8_t)(speed >> 2);
	(void)job_id;  // job_id не входит в CAN-кадр
	}

void Packer_CreateHomeMotorMsg(uint8_t motor_id, uint16_t speed,
		uint32_t job_id, CAN_Message_t* out_msg)
{
	// MOTOR_HOME (0x0102): DLC=5
    // [0-1] cmd_code | [2] motor_id | [3-4] speed (LE, UINT16 — полный диапазон)
    packer_init_msg(out_msg, CAN_CMD_MOTOR_HOME, 5);
    out_msg->data[2] = motor_id;
    pack_u16_le(&out_msg->data[3], speed);
    (void)job_id;
    }

void Packer_CreateStartContinuousMotorMsg(uint8_t motor_id, uint8_t speed,
		uint32_t job_id, CAN_Message_t* out_msg)
{
	// MOTOR_START_CONTINUOUS (0x0103): DLC=4
    // [0-1] cmd_code | [2] motor_id | [3] speed (UINT8)
    packer_init_msg(out_msg, CAN_CMD_MOTOR_START_CONTINUOUS, 4);
    out_msg->data[2] = motor_id;
    out_msg->data[3] = speed;
    (void)job_id;
    }

void Packer_CreateStopMotorMsg(uint8_t motor_id,
		uint32_t job_id, CAN_Message_t* out_msg)
{
	// MOTOR_STOP (0x0104): DLC=3
    // [0-1] cmd_code | [2] motor_id
    packer_init_msg(out_msg, CAN_CMD_MOTOR_STOP, 3);
    out_msg->data[2] = motor_id;
    (void)job_id;
    }

void Packer_CreateStartPumpMsg(uint8_t pump_id,
		uint32_t job_id, CAN_Message_t* out_msg)
{
	// PUMP_START (0x0202): DLC=3
	// [0-1] cmd_code | [2] pump_id
	packer_init_msg(out_msg, CAN_CMD_PUMP_START, 3);
    out_msg->data[2] = pump_id;
    (void)job_id;
    }

void Packer_CreateStopPumpMsg(uint8_t pump_id,
		uint32_t job_id, CAN_Message_t* out_msg)
{
	// PUMP_STOP (0x0203): DLC=3
	// [0-1] cmd_code | [2] pump_id
	packer_init_msg(out_msg, CAN_CMD_PUMP_STOP, 3);
	out_msg->data[2] = pump_id;
    (void)job_id;
    }

void Packer_CreatePerformScanMsg(uint8_t photometer_id, uint8_t wavelength_mask,
		uint32_t job_id, CAN_Message_t* out_msg)
{
	// PHOTOMETER_SCAN (0x0401): DLC=4
    // [0-1] cmd_code | [2] photometer_id | [3] wavelength_mask
    packer_init_msg(out_msg, CAN_CMD_PHOTOMETER_SCAN, 4);
    out_msg->data[2] = photometer_id;
    out_msg->data[3] = wavelength_mask;
    (void)job_id;
    }


// ============================================================
// Функция-распаковщик (Executor → Conductor)
// ============================================================

bool Packer_ParseCanResponse(const CAN_Message_t* in_msg, CAN_Response_t* out_response)
{
	if (in_msg == NULL || out_response == NULL) {
    return false;
    }

	memset(out_response, 0, sizeof(CAN_Response_t));
	// Извлекаем поля из 29-bit CAN ID
    out_response->msg_type    = CAN_GET_MSG_TYPE(in_msg->id);
    out_response->source_addr = CAN_GET_SRC_ADDR(in_msg->id);

    switch (out_response->msg_type) {
    	case CAN_MSG_TYPE_ACK:   // DLC=4: [0-1] cmd_code + [2-3] error_code (=0)

    	case CAN_MSG_TYPE_NACK:  // DLC=4: [0-1] cmd_code + [2-3] error_code
    		if (in_msg->dlc < 4) return false;
    		out_response->command_code = unpack_u16_le(&in_msg->data[0]);
    		out_response->error_code   = unpack_u16_le(&in_msg->data[2]);
    		break;

    	case CAN_MSG_TYPE_DATA_DONE_LOG:
    		if (in_msg->dlc < 1) return false;
    		out_response->sub_type = in_msg->data[0];

    switch (out_response->sub_type) {
    	case CAN_SUB_TYPE_DONE:  // DLC=3: [0] 0x01 | [1-2] cmd_code
    		if (in_msg->dlc < 3) return false;
    		out_response->command_code = unpack_u16_le(&in_msg->data[1]);
    		break;

    	case CAN_SUB_TYPE_DATA:  // DLC=3-8: [0] 0x02 | [1] seq_info | [2..N] data
    		if (in_msg->dlc < 3) return false;
    		out_response->data_len = in_msg->dlc - 2;
    		for (uint8_t i = 0; i < out_response->data_len && i < 6; i++) {
    			out_response->data[i] = in_msg->data[2 + i];
    			}
    		break;

    	case CAN_SUB_TYPE_LOG:   // DLC=3-8: [0] 0x03 | [1] log_level | [2..N] message
    		if (in_msg->dlc < 3) return false;
    		out_response->data_len = in_msg->dlc - 2;
    		for (uint8_t i = 0; i < out_response->data_len && i < 6; i++) {
            out_response->data[i] = in_msg->data[2 + i];
            }
    		break;

    	default:
    		return false;
    		}
    	break;

    	case CAN_MSG_TYPE_COMMAND:  // Дирижер не должен получать COMMAND
    		default:
    			return false;
    			}
    return true;
}


