#ifndef CAN_PROTOCOL_H_
#define CAN_PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Приоритеты (биты 28-26 CAN ID)
// ============================================================
#define CAN_PRIORITY_HIGH       0   // Команды от дирижера
#define CAN_PRIORITY_NORMAL     1   // Ответы от исполнителей

// ============================================================
// Типы сообщений (биты 25-24 CAN ID)
// ============================================================
#define CAN_MSG_TYPE_COMMAND        0   // Команда (Conductor -> Executor)
#define CAN_MSG_TYPE_ACK            1   // Подтверждение (Executor -> Conductor)
#define CAN_MSG_TYPE_NACK           2   // Ошибка (Executor -> Conductor)
#define CAN_MSG_TYPE_DATA_DONE_LOG  3   // DATA/DONE/LOG (Executor -> Conductor)

// ============================================================
// Подтипы для MSG_TYPE_DATA_DONE_LOG (первый байт payload)
// ============================================================
#define CAN_SUB_TYPE_DONE       0x01
#define CAN_SUB_TYPE_DATA       0x02
#define CAN_SUB_TYPE_LOG        0x03

// ============================================================
// Адреса узлов (биты 23-16 dst, 15-8 src)
// ============================================================
#define CAN_ADDR_BROADCAST      0x00
#define CAN_ADDR_HOST           0x01
#define CAN_ADDR_CONDUCTOR      0x10
#define CAN_ADDR_MOTOR_BOARD    0x20  // Наш адрес (плата шаговых двигателей)

// ============================================================
// Типы устройств (для команды INFO)
// ============================================================
#define CAN_DEVICE_TYPE_MOTION  0x01  // Плата управления моторами

// ============================================================
// Коды команд моторов (байты 0-1 payload, Little-Endian)
// ============================================================
#define CAN_CMD_MOTOR_ROTATE            0x0101
#define CAN_CMD_MOTOR_HOME              0x0102
#define CAN_CMD_MOTOR_START_CONTINUOUS  0x0103
#define CAN_CMD_MOTOR_STOP              0x0104

// ============================================================
// Сервисные команды (0xF001 - 0xF00F)
// ============================================================
#define CAN_CMD_SRV_GET_DEVICE_INFO     0xF001  // Получить тип и версию
#define CAN_CMD_SRV_REBOOT              0xF002  // Перезагрузка
#define CAN_CMD_SRV_FLASH_COMMIT        0xF003  // Сохранить настройки в Flash
#define CAN_CMD_SRV_GET_UID             0xF004  // Получить Unique ID (3 пакета)
#define CAN_CMD_SRV_SET_NODE_ID         0xF005  // Установить новый CAN NodeID

// Магический ключ для опасных операций
#define SRV_MAGIC_REBOOT                0x55AA  // Ключ для перезагрузки

// ============================================================
// Макрос построения 29-bit Extended CAN ID
// ============================================================
#define CAN_BUILD_ID(priority, msg_type, dst_addr, src_addr) \
    ((uint32_t)(((priority) & 0x07) << 26) | \
                 (((msg_type) & 0x03) << 24) | \
                 (((dst_addr) & 0xFF) << 16) | \
                 (((src_addr) & 0xFF) << 8))

// ============================================================
// Макросы извлечения полей из 29-bit CAN ID
// ============================================================
#define CAN_GET_PRIORITY(id)    ((uint8_t)(((id) >> 26) & 0x07))
#define CAN_GET_MSG_TYPE(id)    ((uint8_t)(((id) >> 24) & 0x03))
#define CAN_GET_DST_ADDR(id)    ((uint8_t)(((id) >> 16) & 0xFF))
#define CAN_GET_SRC_ADDR(id)    ((uint8_t)(((id) >> 8)  & 0xFF))

// ============================================================
// Коды ошибок для NACK-ответов
// ============================================================
#define CAN_ERR_NONE                0x0000
#define CAN_ERR_UNKNOWN_CMD         0x0001
#define CAN_ERR_INVALID_MOTOR_ID    0x0002
#define CAN_ERR_MOTOR_BUSY          0x0003
#define CAN_ERR_INVALID_KEY         0x0004
#define CAN_ERR_FLASH_WRITE         0x0005

// ============================================================
// Прототипы функций ответов (реализованы в task_can_handler.c)
// ============================================================
void CAN_SendAck(uint16_t cmd_code);
void CAN_SendNackPublic(uint16_t cmd_code, uint16_t error_code);
void CAN_SendDone(uint16_t cmd_code, uint8_t device_id);
void CAN_SendData(uint16_t cmd_code, uint8_t *data, uint8_t len);

#endif /* CAN_PROTOCOL_H_ */
