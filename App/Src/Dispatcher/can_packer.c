/*
 * can_packer.c
 *
 * МОДЕРНИЗАЦИЯ ПОД СТАНДАРТ ЭКОСИСТЕМЫ DDS-240 (Директива 2.0)
 * -----------------------------------------------------------
 * Реализация упаковщика и распаковщика CAN-кадров.
 * Обеспечивает строгий DLC=8 и унифицированное размещение параметров.
 *
 *  Created on: Dec 4, 2025 (Updated: Apr 9, 2026)
 *      Author: andrey (Gemini CLI)
 */

#include "Dispatcher/can_packer.h"
#include <string.h> // Для memset

// ============================================================================
// ---                  ВНУТРЕННИЕ ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ                  ---
// ============================================================================

/**
 * @brief Упаковка uint16_t в Little-Endian (2 байта)
 */
static void pack_u16_le(uint8_t* dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
}

/**
 * @brief Упаковка uint32_t/int32_t в Little-Endian (4 байта)
 */
static void pack_u32_le(uint8_t* dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF);
    dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

/**
 * @brief Распаковка uint16_t из Little-Endian
 */
static uint16_t unpack_u16_le(const uint8_t* src)
{
    return (uint16_t)(src[0] | ((uint16_t)src[1] << 8));
}

/**
 * @brief Унифицированное заполнение Payload (Директива 2.0)
 * Структура [8 байт]: [0-1] CMD, [2] CH, [3-6] PARAM, [7] RSV
 */
static void packer_fill_payload(CAN_Message_t* msg, uint16_t cmd, uint8_t ch, uint32_t param)
{
    memset(msg->data, 0, 8);
    pack_u16_le(&msg->data[0], cmd);   // Код команды
    msg->data[2] = ch;                 // Индекс канала
    pack_u32_le(&msg->data[3], param); // Параметр (32 бита)
    msg->data[7] = 0x00;               // Резерв
}

/**
 * @brief Инициализация заголовка сообщения
 */
static void packer_init_header(CAN_Message_t* msg, uint8_t dst_addr)
{
    msg->id = CAN_BUILD_ID(CAN_PRIORITY_HIGH, CAN_MSG_TYPE_COMMAND, 
                           dst_addr, CAN_ADDR_CONDUCTOR);
    msg->dlc = 8;           // СТРОГИЙ DLC=8 по Директиве 2.0
    msg->is_extended = true;
}

// ============================================================================
// ---               РЕАЛИЗАЦИЯ ФУНКЦИЙ УПАКОВЩИКА (Motion)                 ---
// ============================================================================

void Packer_CreateRotateMotorMsg(uint8_t ch_idx, int32_t steps, uint16_t speed, CAN_Message_t* out_msg)
{
    packer_init_header(out_msg, CAN_ADDR_MOTOR_BOARD);
    // Для ROTATE скорость упаковывается в байт 7 (резервный) как speed/4 
    // или передается как часть 32-битного параметра. 
    // Согласно CONDUCTOR_INTEGRATION_GUIDE 8.4: steps (32б), speed_div_4 (байт 7)
    packer_fill_payload(out_msg, CAN_CMD_MOTOR_ROTATE, ch_idx, (uint32_t)steps);
    out_msg->data[7] = (uint8_t)(speed >> 2); 
}

void Packer_CreateHomeMotorMsg(uint8_t ch_idx, uint16_t speed, CAN_Message_t* out_msg)
{
    packer_init_header(out_msg, CAN_ADDR_MOTOR_BOARD);
    packer_fill_payload(out_msg, CAN_CMD_MOTOR_HOME, ch_idx, (uint32_t)speed);
}

void Packer_CreateStartContinuousMotorMsg(uint8_t ch_idx, uint16_t speed, CAN_Message_t* out_msg)
{
    packer_init_header(out_msg, CAN_ADDR_MOTOR_BOARD);
    // Для непрерывного вращения скорость делится на 100 (GUIDE 8.4)
    packer_fill_payload(out_msg, CAN_CMD_MOTOR_START_CONTINUOUS, ch_idx, (uint32_t)(speed / 100));
}

void Packer_CreateStopMotorMsg(uint8_t ch_idx, CAN_Message_t* out_msg)
{
    packer_init_header(out_msg, CAN_ADDR_MOTOR_BOARD);
    packer_fill_payload(out_msg, CAN_CMD_MOTOR_STOP, ch_idx, 0);
}

// ============================================================================
// ---              РЕАЛИЗАЦИЯ ФУНКЦИЙ УПАКОВЩИКА (Fluidics)                ---
// ============================================================================

void Packer_CreatePumpRunDurationMsg(uint8_t ch_idx, uint32_t duration_ms, CAN_Message_t* out_msg)
{
    packer_init_header(out_msg, CAN_ADDR_PUMP_BOARD);
    packer_fill_payload(out_msg, CAN_CMD_PUMP_RUN_DURATION, ch_idx, duration_ms);
}

void Packer_CreatePumpStartMsg(uint8_t ch_idx, uint32_t timeout_ms, CAN_Message_t* out_msg)
{
    packer_init_header(out_msg, CAN_ADDR_PUMP_BOARD);
    packer_fill_payload(out_msg, CAN_CMD_PUMP_START, ch_idx, timeout_ms);
}

void Packer_CreatePumpStopMsg(uint8_t ch_idx, CAN_Message_t* out_msg)
{
    packer_init_header(out_msg, CAN_ADDR_PUMP_BOARD);
    packer_fill_payload(out_msg, CAN_CMD_PUMP_STOP, ch_idx, 0);
}

void Packer_CreateValveOpenMsg(uint8_t ch_idx, uint32_t timeout_ms, CAN_Message_t* out_msg)
{
    packer_init_header(out_msg, CAN_ADDR_PUMP_BOARD);
    packer_fill_payload(out_msg, CAN_CMD_VALVE_OPEN, ch_idx, timeout_ms);
}

void Packer_CreateValveCloseMsg(uint8_t ch_idx, CAN_Message_t* out_msg)
{
    packer_init_header(out_msg, CAN_ADDR_PUMP_BOARD);
    packer_fill_payload(out_msg, CAN_CMD_VALVE_CLOSE, ch_idx, 0);
}

// ============================================================================
// ---               РЕАЛИЗАЦИЯ ФУНКЦИЙ УПАКОВЩИКА (Thermo)                 ---
// ============================================================================

void Packer_CreateGetTempMsg(uint8_t ch_idx, CAN_Message_t* out_msg)
{
    packer_init_header(out_msg, CAN_ADDR_THERMO_BOARD);
    packer_fill_payload(out_msg, CAN_CMD_THERMO_GET_TEMP, ch_idx, 0);
}

void Packer_CreateGetAllTempsMsg(CAN_Message_t* out_msg)
{
    packer_init_header(out_msg, CAN_ADDR_THERMO_BOARD);
    packer_fill_payload(out_msg, CAN_CMD_THERMO_GET_ALL, 0, 0);
}

// ============================================================================
// ---               РЕАЛИЗАЦИЯ ФУНКЦИЙ УПАКОВЩИКА (Service)                ---
// ============================================================================

void Packer_CreateRebootMsg(uint8_t dst_addr, CAN_Message_t* out_msg)
{
    packer_init_header(out_msg, dst_addr);
    packer_fill_payload(out_msg, CAN_CMD_SRV_REBOOT, 0, SRV_MAGIC_REBOOT);
}

void Packer_CreateFactoryResetMsg(uint8_t dst_addr, CAN_Message_t* out_msg)
{
    packer_init_header(out_msg, dst_addr);
    packer_fill_payload(out_msg, CAN_CMD_SRV_FACTORY_RESET, 0, SRV_MAGIC_FACTORY_RESET);
}

// ============================================================================
// ---              РЕАЛИЗАЦИЯ ФУНКЦИЙ РАСПАКОВЩИКА (Unpacker)               ---
// ============================================================================

bool Packer_ParseCanResponse(const CAN_Message_t* in_msg, CAN_Response_t* out_response)
{
    if (in_msg == NULL || out_response == NULL) return false;

    memset(out_response, 0, sizeof(CAN_Response_t));
    out_response->msg_type    = CAN_GET_MSG_TYPE(in_msg->id);
    out_response->source_addr = CAN_GET_SRC_ADDR(in_msg->id);

    switch (out_response->msg_type) {
        case CAN_MSG_TYPE_ACK:
        case CAN_MSG_TYPE_NACK:
            if (in_msg->dlc < 4) return false;
            out_response->command_code = unpack_u16_le(&in_msg->data[0]);
            out_response->error_code   = unpack_u16_le(&in_msg->data[2]);
            break;

        case CAN_MSG_TYPE_DATA_DONE_LOG:
            if (in_msg->dlc < 1) return false;
            out_response->sub_type = in_msg->data[0];

            switch (out_response->sub_type) {
                case CAN_SUB_TYPE_DONE:
                    if (in_msg->dlc < 3) return false;
                    out_response->command_code = unpack_u16_le(&in_msg->data[1]);
                    // Индекс канала может быть в data[3] в некоторых прошивках
                    if (in_msg->dlc >= 4) out_response->ch_idx = in_msg->data[3];
                    break;

                case CAN_SUB_TYPE_DATA:
                    if (in_msg->dlc < 3) return false;
                    out_response->ch_idx = in_msg->data[1]; // Байт 1: ch_idx
                    out_response->data_len = in_msg->dlc - 2;
                    for (uint8_t i = 0; i < out_response->data_len && i < 6; i++) {
                        out_response->data[i] = in_msg->data[2 + i];
                    }
                    break;

                case CAN_SUB_TYPE_LOG:
                    out_response->data_len = in_msg->dlc - 1;
                    for (uint8_t i = 0; i < out_response->data_len && i < 7; i++) {
                        out_response->data[i] = in_msg->data[1 + i];
                    }
                    break;

                default:
                    return false;
            }
            break;

        default:
            return false;
    }

    return true;
}
