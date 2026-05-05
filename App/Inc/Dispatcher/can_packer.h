/*
 * can_packer.h
 *
 * МОДЕРНИЗАЦИЯ ПОД СТАНДАРТ ЭКОСИСТЕМЫ DDS-240 (Директива 2.0)
 * -----------------------------------------------------------
 * Данный модуль является "Умным Шлюзом" между Big-endian миром Хоста 
 * (протокол CM>) и Little-endian миром Исполнителей (CAN 2.0B).
 *
 * Ключевые принципы:
 * 1. Строгий DLC=8 для всех команд (упрощение фильтрации на bxCAN).
 * 2. Унифицированный Payload: [0-1] Код, [2] Канал, [3-6] Параметр, [7] Резерв.
 * 3. Физическая адресация: переход на NodeID (0x20, 0x30, 0x40) и 0-based индексы.
 *
 *  Created on: Dec 4, 2025 (Updated: Apr 9, 2026)
 *      Author: andrey (Gemini CLI)
 */

#ifndef INC_DISPATCHER_CAN_PACKER_H_
#define INC_DISPATCHER_CAN_PACKER_H_

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// ---                  СТРУКТУРЫ ДАННЫХ (Data Structures)                  ---
// ============================================================================

/**
 * @brief Абстрактная структура CAN-кадра.
 * Используется внутри Дирижера для передачи между задачами (FreeRTOS Queues).
 * По Директиве 2.0 поле data всегда содержит 8 байт для исходящих команд.
 */
typedef struct {
    uint32_t id;            // 29-битный расширенный идентификатор (Extended ID)
    uint8_t  data[8];       // Полезная нагрузка (Payload)
    uint8_t  dlc;           // Длина данных (для команд всегда 8)
    bool     is_extended;   // Флаг расширенного кадра (всегда true)
} CAN_Message_t;

/**
 * @brief Результат парсинга ответа от Исполнителя.
 * Позволяет Диспетчеру (JobManager) принимать решения на основе типа ответа.
 */
typedef struct {
	uint8_t  msg_type;      // Тип из CAN ID: ACK (1), NACK (2), DATA_DONE_LOG (3)
    uint8_t  source_addr;   // NodeID отправителя (0x20, 0x30, 0x40)
    uint16_t command_code;  // Код команды (из байт 0-1 или 1-2 payload)
    uint16_t error_code;    // Код ошибки NACK (0 = OK)
    uint8_t  sub_type;      // Подтип для типа 3: DONE (0x01), DATA (0x02), LOG (0x03)
    uint8_t  ch_idx;        // Индекс физического канала (0-15)

    union {
    	uint8_t  raw[4];    // Сырые байты данных (для DATA)
    	uint32_t val32;     // Универсальное 32-битное значение (для температуры, шагов и т.д.)
    	char     log[7];    // Текстовое сообщение (для LOG)
    	} payload;
    	uint8_t  data_len;      // Длина полезных данных в payload
} CAN_Response_t;



// ============================================================================
// ---                 КОНСТАНТЫ ТРАНСПОРТНОГО УРОВНЯ                       ---
// ============================================================================

// --- Приоритеты (биты 28-26 ID) ---
#define CAN_PRIORITY_HIGH       0   // Для управляющих команд Дирижера
#define CAN_PRIORITY_NORMAL     1   // Для ответов и логов Исполнителей

// --- Типы сообщений (биты 25-24 ID) ---
#define CAN_MSG_TYPE_COMMAND        0   // Запрос действия (Conductor -> Executor)
#define CAN_MSG_TYPE_ACK            1   // Подтверждение приема (OK)
#define CAN_MSG_TYPE_NACK           2   // Ошибка приема/параметров (Error)
#define CAN_MSG_TYPE_DATA_DONE_LOG  3   // Событийный обмен (Data, Done, Log)

// --- Размер полезной нагрузки (Directive 2.0) ---
#define CAN_PAYLOAD_SIZE            8   // Фиксированный размер кадра для всех команд и ответов

// --- Подтипы для типа DATA_DONE_LOG (байт 0 payload) ---
#define CAN_SUB_TYPE_DONE           0x01 // Физическое завершение действия
#define CAN_SUB_TYPE_DATA           0x02 // Результат измерения (напр. Температура)
#define CAN_SUB_TYPE_LOG            0x03 // Текстовое сообщение для отладки

// --- Сетевая топология (NodeID, биты 23-8 ID) ---
#define CAN_ADDR_BROADCAST      0x00    // Широковещательный адрес
#define CAN_ADDR_CONDUCTOR      0x10    // Адрес Дирижера (Master)
#define CAN_ADDR_MOTOR_BOARD    0x20    // Плата Motion (Шаговые двигатели)
#define CAN_ADDR_PUMP_BOARD     0x30    // Плата Fluidic (Насосы и Клапаны)
#define CAN_ADDR_THERMO_BOARD   0x40    // Плата Thermo (Датчики температуры)


// ============================================================================
// ---                  РЕЕСТР КОМАНД (Command Registry)                    ---
// ---           Все коды упаковываются в байты 0-1 (Little-Endian)         ---
// ============================================================================

// --- Группа 1: Motion (0x01xx) ---
#define CAN_CMD_MOTOR_ROTATE            0x0101 // Вращение на шаги
#define CAN_CMD_MOTOR_HOME              0x0102 // Поиск начальной точки (0)
#define CAN_CMD_MOTOR_START_CONTINUOUS  0x0103 // Непрерывное вращение (Миксер)
#define CAN_CMD_MOTOR_STOP              0x0104 // Экстренная остановка

// --- Группа 2: Fluidics (0x02xx) ---
#define CAN_CMD_PUMP_RUN_DURATION       0x0201 // Запуск насоса на время (мс)
#define CAN_CMD_PUMP_START              0x0202 // Включить насос (ON)
#define CAN_CMD_PUMP_STOP               0x0203 // Выключить насос (OFF)
#define CAN_CMD_VALVE_OPEN              0x0204 // Открыть клапан
#define CAN_CMD_VALVE_CLOSE             0x0205 // Закрыть клапан

// --- Группа 3: Thermo/Sensors (0x90xx) ---
#define CAN_CMD_THERMO_GET_TEMP         0x9011 // Запрос температуры датчика
#define CAN_CMD_THERMO_GET_ALL          0x9010 // Запрос данных со всех датчиков платы

// --- Группа 4: Service & Maintenance (0xFxxx) ---
#define CAN_CMD_SRV_GET_INFO            0xF001 // Запрос версии и типа платы
#define CAN_CMD_SRV_REBOOT              0xF002 // Программная перезагрузка
#define CAN_CMD_SRV_COMMIT              0xF003 // Сохранение RAM-настроек во Flash
#define CAN_CMD_SRV_SET_NODE_ID         0xF005 // Изменение сетевого адреса (NodeID)
#define CAN_CMD_SRV_FACTORY_RESET       0xF006 // Сброс к заводским установкам
#define CAN_CMD_SRV_SCAN_1WIRE          0xF101 // Запуск сканирования шины 1-Wire

// --- Защитные ключи (Magic Keys) ---
#define SRV_MAGIC_REBOOT                0x55AA // Ключ для команды REBOOT
#define SRV_MAGIC_FACTORY_RESET         0xDEAD // Ключ для команды FACTORY_RESET


// ============================================================================
// ---           РЕЕСТР ОШИБОК NACK (Error Registry - Stage 1.4)            ---
// ---        Синхронизировано с экосистемой DDS-240 (Директива 2.0)         ---
// ============================================================================
#define CAN_NACK_OK                     0x0000 // Нет ошибки
#define CAN_NACK_ERR_UNKNOWN_CMD        0x0001 // Неизвестная команда
#define CAN_NACK_ERR_INVALID_CH         0x0002 // Неверный индекс канала
#define CAN_NACK_ERR_DEVICE_FAILURE     0x0003 // Аппаратный сбой (сенсор/мотор)
#define CAN_NACK_ERR_INVALID_KEY        0x0004 // Неверный Magic Key
#define CAN_NACK_ERR_FLASH_WRITE        0x0005 // Ошибка записи во Flash
#define CAN_NACK_ERR_INVALID_PARAM      0x0006 // Некорректный параметр/DLC
#define CAN_NACK_ERR_BUSY               0x0007 // Устройство занято



// ============================================================================
// ---                 МАКРОСЫ ФОРМИРОВАНИЯ CAN ID                          ---
// ============================================================================

/**
 * @brief Сборка 29-битного идентификатора кадра.
 * Биты: [28-26] Приоритет | [25-24] Тип | [23-16] Dst | [15-8] Src
 */
#define CAN_BUILD_ID(priority, msg_type, dst_addr, src_addr) \
    ((uint32_t)(((priority) & 0x07) << 26) | \
                (((msg_type) & 0x03) << 24) | \
                (((dst_addr) & 0xFF) << 16) | \
                (((src_addr) & 0xFF) << 8))

/**
 * @brief Извлечение полей из 29-битного ID (для входящих ответов)
 */
#define CAN_GET_PRIORITY(id)    ((uint8_t)(((id) >> 26) & 0x07))
#define CAN_GET_MSG_TYPE(id)    ((uint8_t)(((id) >> 24) & 0x03))
#define CAN_GET_DST_ADDR(id)    ((uint8_t)(((id) >> 16) & 0xFF))
#define CAN_GET_SRC_ADDR(id)    ((uint8_t)(((id) >> 8)  & 0xFF))


// ============================================================================
// ---               ПРОТОТИПЫ ФУНКЦИЙ УПАКОВЩИКА (Packer)                  ---
// ---      Все функции гарантированно создают кадр с DLC=8 (Directive 2.0)  ---
// ============================================================================

// --- Секция Motion (Моторы 0-7) ---
void Packer_CreateRotateMotorMsg(uint8_t ch_idx, int32_t steps, uint16_t speed, CAN_Message_t* out_msg);
void Packer_CreateHomeMotorMsg(uint8_t ch_idx, uint16_t speed, CAN_Message_t* out_msg);
void Packer_CreateStartContinuousMotorMsg(uint8_t ch_idx, uint16_t speed, CAN_Message_t* out_msg);
void Packer_CreateStopMotorMsg(uint8_t ch_idx, CAN_Message_t* out_msg);

// --- Секция Fluidics (Насосы 0-12, Клапаны 13-15) ---
void Packer_CreatePumpRunDurationMsg(uint8_t ch_idx, uint32_t duration_ms, CAN_Message_t* out_msg);
void Packer_CreatePumpStartMsg(uint8_t ch_idx, uint32_t timeout_ms, CAN_Message_t* out_msg);
void Packer_CreatePumpStopMsg(uint8_t ch_idx, CAN_Message_t* out_msg);
void Packer_CreateValveOpenMsg(uint8_t ch_idx, uint32_t timeout_ms, CAN_Message_t* out_msg);
void Packer_CreateValveCloseMsg(uint8_t ch_idx, CAN_Message_t* out_msg);

// --- Секция Thermo (Датчики 0-7) ---
void Packer_CreateGetTempMsg(uint8_t ch_idx, CAN_Message_t* out_msg);
void Packer_CreateGetAllTempsMsg(CAN_Message_t* out_msg);

// --- Секция Service (Сервисные команды) ---
void Packer_CreateGetInfoMsg(uint8_t dst_addr, CAN_Message_t* out_msg);
void Packer_CreateGetUidMsg(uint8_t dst_addr, CAN_Message_t* out_msg);
void Packer_CreateSetNodeIdMsg(uint8_t dst_addr, uint8_t new_node_id, CAN_Message_t* out_msg);
void Packer_CreateRebootMsg(uint8_t dst_addr, CAN_Message_t* out_msg);
void Packer_CreateFactoryResetMsg(uint8_t dst_addr, CAN_Message_t* out_msg);



// ============================================================================
// ---              ПРОТОТИПЫ ФУНКЦИЙ РАСПАКОВЩИКА (Unpacker)               ---
// ============================================================================

/**
 * @brief Разбор входящего CAN-кадра от Исполнителя.
 * Выполняет первичную проверку формата и заполняет структуру CAN_Response_t.
 */
bool Packer_ParseCanResponse(const CAN_Message_t* in_msg, CAN_Response_t* out_response);

#endif /* INC_DISPATCHER_CAN_PACKER_H_ */
