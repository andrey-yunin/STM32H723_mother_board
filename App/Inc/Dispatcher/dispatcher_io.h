/*
 * dispatcher_io.h
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_DISPATCHER_IO_H_
#define INC_DISPATCHER_DISPATCHER_IO_H_

#include <stdint.h> // Для uint8_t, uint16_t
#include "app_config.h"

/**
 * @brief Структура для отправки данных в задачу USB_TX.
 *        Позволяет передавать как бинарные пакеты, так и текстовые строки.
 */
typedef struct {
	uint8_t data[APP_USB_RESP_MAX_LEN];
	uint16_t length;
	} USB_TxPacket_t;



/**
* @brief Отправляет строку-ответ в очередь на передачу по USB.
*        Эта функция предназначена для всех внутренних модулей диспетчера,
*        которым нужно отправить сообщение пользователю.
*
* @param message Указатель на null-terminated строку для отправки.
*/
void Dispatcher_SendUsbResponse(const char* message);

/**
 * @brief Отправляет стандартный бинарный ACK-ответ.
 * @param command_code Код команды.
 */

void Dispatcher_SendAck(uint16_t command_code);

/**
 * @brief Отправляет стандартный бинарный NACK-ответ.
 * @param command_code Код команды.
 * @param error_code Код ошибки.
 */
void Dispatcher_SendNack(uint16_t command_code, uint16_t error_code);

/**
 * @brief Отправляет стандартный бинарный DONE-ответ (команда выполнена).
 * @param command_code Код команды, на которую отправляется ответ.
 * @param status Статус выполнения команды (0x0000 = OK, другие коды для успеха).
 */
void Dispatcher_SendDone(uint16_t command_code, uint16_t status);

/**
 * @brief Отправляет стандартный бинарный ERROR-ответ (команда не выполнена из-за ошибки).
 * @param command_code Код команды, на которую отправляется ответ.
 * @param error_code Код ошибки (из errors.md).
 */
void Dispatcher_SendError(uint16_t command_code, uint16_t error_code);

/**
 *
 * @brief Отправляет бинарный пакет с данными (DATA-ответ).
 * @param command_code Код команды, на которую отправляется ответ.
 * @param data Указатель на буфер с данными.
 * @param data_len Длина данных.
*/

void Dispatcher_SendData(uint16_t command_code, uint8_t response_type, uint16_t status, const uint8_t* data, uint16_t data_len);


#endif /* INC_DISPATCHER_DISPATCHER_IO_H_ */
