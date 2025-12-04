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

// --- Определения для CAN-сообщений ---

/**
 * @brief Структура для представления CAN-сообщения.
 *        Это заглушка, реальная структура будет зависеть от вашей FDCAN периферии
 *        и протокола обмена.
 */
typedef struct {
	uint32_t id;        // ID сообщения (адрес исполнителя + тип команды)
    uint8_t data[8];    // Полезные данные CAN-сообщения (до 8 байт для Classic CAN)
    uint8_t dlc;        // Длина данных
// Дополнительные поля могут быть добавлены (например, для FDCAN, фильтров и т.д.)
} CAN_Message_t;

/**
 * @brief Структура для представления ответа от исполнителя по CAN.
 *        Пока заглушка.
 */
typedef struct {
	uint32_t job_id;    // ID задания, к которому относится ответ
    uint8_t executor_id; // ID исполнителя, от которого пришел ответ
    bool status_ok;     // Статус выполнения действия (true=успех, false=ошибка)
    // Дополнительные поля по необходимости
} CAN_Response_t;


// --- Прототипы функций-упаковщиков для JobManager ---

/**
 * @brief Создает CAN-сообщение для поворота мотора.
 */
void Packer_CreateRotateMotorMsg(uint8_t motor_id, int32_t steps, uint16_t speed, uint32_t job_id, CAN_Message_t* out_msg);

/**
 * @brief Создает CAN-сообщение для запуска насоса.
 */
void Packer_CreateStartPumpMsg(uint8_t pump_id, uint32_t job_id, CAN_Message_t* out_msg);

/**
 * @brief Создает CAN-сообщение для остановки насоса.
 */
void Packer_CreateStopPumpMsg(uint8_t pump_id, uint32_t job_id, CAN_Message_t* out_msg);

/**
 * @brief Создает CAN-сообщение для поиска "дома" мотора.
 */
void Packer_CreateHomeMotorMsg(uint8_t motor_id, uint16_t speed, uint32_t job_id, CAN_Message_t* out_msg);

// --- Прототипы функций-распаковщиков для task_can_handler (для будущей реализации) ---

/**
 * @brief Распаковывает входящее CAN-сообщение в CAN_Response_t.
 *        Эта функция будет использоваться CAN-обработчиком.
 */
bool Packer_ParseCanResponse(const CAN_Message_t* in_msg, CAN_Response_t* out_response);

#endif /* INC_DISPATCHER_CAN_PACKER_H_ */
