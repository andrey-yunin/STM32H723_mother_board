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

typedef enum {
	HOST_RESPONSE_TYPE_NACK  = 0x00U,
	HOST_RESPONSE_TYPE_ACK   = 0x01U,
	HOST_RESPONSE_TYPE_DONE  = 0x02U,
	HOST_RESPONSE_TYPE_DATA  = 0x03U,
	HOST_RESPONSE_TYPE_ERROR = 0x04U
} HostResponseType_t;

typedef enum {
	HOST_STATUS_OK = 0x0000U
} HostStatus_t;

typedef enum {
	HOST_ERR_OK              = 0x0000U,
	HOST_ERR_GENERAL         = 0x0001U,
	HOST_ERR_UNKNOWN_CMD     = 0x0002U,
	HOST_ERR_INVALID_PARAM   = 0x0003U,
	HOST_ERR_BUSY            = 0x0004U,
	HOST_ERR_NOT_INIT        = 0x0005U,
	HOST_ERR_TIMEOUT         = 0x0006U,
	HOST_ERR_CRC             = 0x0007U,
	HOST_ERR_PACKET_FORMAT   = 0x0008U,
	HOST_ERR_BUFFER_OVERFLOW = 0x0009U,
	HOST_ERR_NOT_SUPPORTED   = 0x000AU,
	HOST_ERR_EMERGENCY_STOP  = 0x000BU,
	HOST_ERR_COVER_OPEN      = 0x000CU,
	HOST_ERR_LOW_LIQUID      = 0x000DU,
	HOST_ERR_HARDWARE        = 0x000EU,
	HOST_ERR_CALIBRATION     = 0x000FU
} HostErrorCode_t;



/**
* @brief Legacy wrapper для текстовой диагностики.
*        Текстовые сообщения направляются в task_logger через log_queue_handle.
*        Host protocol responses должны использовать Dispatcher_SendAck/Done/Error/Data.
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
 * @param status Статус выполнения команды, например HOST_STATUS_OK.
 */
void Dispatcher_SendDone(uint16_t command_code, uint16_t status);

/**
 * @brief Отправляет стандартный бинарный ERROR-ответ (команда не выполнена из-за ошибки).
 * @param command_code Код команды, на которую отправляется ответ.
 * @param error_code Код ошибки HOST_ERR_* из Host_Commands_API/User_Commands/errors.md.
 */
void Dispatcher_SendError(uint16_t command_code, uint16_t error_code);

/**
 *
 * @brief Отправляет бинарный пакет с данными (DATA-ответ).
 * @param command_code Код команды, на которую отправляется ответ.
 * @param data Указатель на буфер с данными.
 * @param data_len Длина данных.
*/

void Dispatcher_SendData(uint16_t command_code,
		HostResponseType_t response_type,
		uint16_t status,
		const uint8_t* data,
		uint16_t data_len);


#endif /* INC_DISPATCHER_DISPATCHER_IO_H_ */
