/*
 * can_packer.h
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_CAN_PACKER_H_
#define INC_DISPATCHER_CAN_PACKER_H_

#include <stdint.h> // Для uint8_t, uint32_t и т.д.
#include <stdbool.h>

// ============================================================
// CAN-кадр: абстрактная структура (не зависит от HAL)
// Используется в can_packer, job_manager, очереди FreeRTOS.
// task_can_handler конвертирует в/из HAL-формат.
// ============================================================

typedef struct {
	uint32_t id;        // 29-bit Extended CAN ID
    uint8_t  data[8];   // Payload (до 8 байт для Classic CAN)
    uint8_t  dlc;       // Data Length Code (0-8)
    bool     is_extended; // true = 29-bit ID, false = 11-bit ID
    } CAN_Message_t;

// ============================================================
// Структура распакованного ответа от исполнителя
// ============================================================

typedef struct {
	uint8_t  msg_type;      // CAN_MSG_TYPE_ACK / NACK / DATA_DONE_LOG
    uint8_t  source_addr;   // Адрес отправителя (исполнителя)
    uint16_t command_code;  // Код команды, на которую ответ
    uint16_t error_code;    // Код ошибки (для ACK/NACK, 0 = нет ошибки)
    uint8_t  sub_type;      // Для msg_type==DATA_DONE_LOG: 0x01=DONE, 0x02=DATA, 0x03=LOG
    uint8_t  data[6];       // Данные (для DATA sub-type)
    uint8_t  data_len;      // Длина данных
    } CAN_Response_t;

// ============================================================
// Константы протокола: 29-bit CAN ID
// По спецификации 2_Frame_Format.md
// ============================================================

// --- Приоритеты (биты 28-26) ---
#define CAN_PRIORITY_HIGH       0   // Команды от дирижера
#define CAN_PRIORITY_NORMAL     1   // Ответы от исполнителей

// --- Типы сообщений (биты 25-24) ---
#define CAN_MSG_TYPE_COMMAND    0   // Команда (Conductor → Executor)
#define CAN_MSG_TYPE_ACK        1   // Подтверждение (Executor → Conductor)
#define CAN_MSG_TYPE_NACK       2   // Ошибка (Executor → Conductor)
#define CAN_MSG_TYPE_DATA_DONE_LOG 3 // DATA/DONE/LOG (Executor → Conductor)

// --- Подтипы для MSG_TYPE_DATA_DONE_LOG (первый байт payload) ---
#define CAN_SUB_TYPE_DONE       0x01
#define CAN_SUB_TYPE_DATA       0x02
#define CAN_SUB_TYPE_LOG        0x03

// --- Адреса узлов (биты 23-16 dst, 15-8 src) ---
#define CAN_ADDR_BROADCAST      0x00
#define CAN_ADDR_HOST           0x01
#define CAN_ADDR_CONDUCTOR      0x10
#define CAN_ADDR_MOTOR_BOARD    0x20  // Плата управления шаговыми двигателями
#define CAN_ADDR_PUMP_BOARD     0x30  // Плата управления насосами/клапанами
#define CAN_ADDR_THERMO_BOARD   0x40  // Плата датчиков температуры

// --- Коды низкоуровневых команд (байты 0-1 payload) ---
#define CAN_CMD_MOTOR_ROTATE          0x0101
#define CAN_CMD_MOTOR_HOME            0x0102
#define CAN_CMD_MOTOR_START_CONTINUOUS 0x0103
#define CAN_CMD_MOTOR_STOP            0x0104
#define CAN_CMD_PUMP_RUN_DURATION     0x0201  // Резерв (не используется в текущей прошивке)
#define CAN_CMD_PUMP_START            0x0202
#define CAN_CMD_PUMP_STOP             0x0203
#define CAN_CMD_PHOTOMETER_SCAN       0x0401

// ============================================================
// Макрос конструирования 29-bit CAN ID
// ============================================================
#define CAN_BUILD_ID(priority, msg_type, dst_addr, src_addr) \
	((uint32_t)(((priority) & 0x07) << 26) | \
			(((msg_type) & 0x03) << 24) | \
			(((dst_addr) & 0xFF) << 16) | \
			(((src_addr) & 0xFF) << 8))

// Макросы извлечения полей из CAN ID
#define CAN_GET_PRIORITY(id)    ((uint8_t)(((id) >> 26) & 0x07))
#define CAN_GET_MSG_TYPE(id)    ((uint8_t)(((id) >> 24) & 0x03))
#define CAN_GET_DST_ADDR(id)    ((uint8_t)(((id) >> 16) & 0xFF))
#define CAN_GET_SRC_ADDR(id)    ((uint8_t)(((id) >> 8)  & 0xFF))

// ============================================================
// Функции-упаковщики (Conductor → Executor)
// ============================================================

/** @brief CAN-кадр для MOTOR_ROTATE (0x0101). DLC=8. */
void Packer_CreateRotateMotorMsg(uint8_t motor_id, int32_t steps, uint16_t speed,
		uint32_t job_id, CAN_Message_t* out_msg);

/** @brief CAN-кадр для MOTOR_HOME (0x0102). DLC=5. */
void Packer_CreateHomeMotorMsg(uint8_t motor_id, uint16_t speed,
		uint32_t job_id, CAN_Message_t* out_msg);

/** @brief CAN-кадр для MOTOR_START_CONTINUOUS (0x0103). DLC=4. */
void Packer_CreateStartContinuousMotorMsg(uint8_t motor_id, uint8_t speed,
		uint32_t job_id, CAN_Message_t* out_msg);

/** @brief CAN-кадр для MOTOR_STOP (0x0104). DLC=3. */
void Packer_CreateStopMotorMsg(uint8_t motor_id,
		uint32_t job_id, CAN_Message_t* out_msg);

/** @brief CAN-кадр для PUMP_START (0x0202). DLC=3. */
void Packer_CreateStartPumpMsg(uint8_t pump_id,
		uint32_t job_id, CAN_Message_t* out_msg);

/** @brief CAN-кадр для PUMP_STOP (0x0203). DLC=3. */
void Packer_CreateStopPumpMsg(uint8_t pump_id,
		uint32_t job_id, CAN_Message_t* out_msg);

/** @brief CAN-кадр для PHOTOMETER_SCAN (0x0401). DLC=4. */
void Packer_CreatePerformScanMsg(uint8_t photometer_id, uint8_t wavelength_mask,
		uint32_t job_id, CAN_Message_t* out_msg);

// ============================================================
// Функция-распаковщик (Executor → Conductor)
// ============================================================

/** @brief Распаковывает входящий CAN-кадр в CAN_Response_t.
*  @return true если кадр успешно распакован, false если формат некорректен. */
bool Packer_ParseCanResponse(const CAN_Message_t* in_msg, CAN_Response_t* out_response);

#endif /* INC_DISPATCHER_CAN_PACKER_H_ */
