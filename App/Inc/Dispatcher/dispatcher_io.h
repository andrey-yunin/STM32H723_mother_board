/*
 * dispatcher_io.h
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_DISPATCHER_IO_H_
#define INC_DISPATCHER_DISPATCHER_IO_H_

#include <stdint.h> // Для uint8_t, uint16_t

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

#endif /* INC_DISPATCHER_DISPATCHER_IO_H_ */
