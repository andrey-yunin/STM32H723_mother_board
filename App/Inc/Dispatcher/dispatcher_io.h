/*
 * dispatcher_io.h
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_DISPATCHER_IO_H_
#define INC_DISPATCHER_DISPATCHER_IO_H_

/**
* @brief Отправляет строку-ответ в очередь на передачу по USB.
*        Эта функция предназначена для всех внутренних модулей диспетчера,
*        которым нужно отправить сообщение пользователю.
*
* @param message Указатель на null-terminated строку для отправки.
*/
void Dispatcher_SendUsbResponse(const char* message);


#endif /* INC_DISPATCHER_DISPATCHER_IO_H_ */
