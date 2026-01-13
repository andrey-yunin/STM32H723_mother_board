/*
 * dispatcher_io.c
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#include "Dispatcher/dispatcher_io.h"
#include "shared_resources.h" // Для доступа к usb_tx_queue_handle
#include "app_config.h"       // Для APP_USB_RESP_MAX_LEN
#include <string.h>           // Для strncpy
#include "cmsis_os.h"         // Для pdMS_TO_TICKS
#include <stdint.h> // Для uint8_t, uint16_t


/**
* @brief Отправляет строку-ответ в очередь на передачу по USB.
*
* @param message Указатель на null-terminated строку для отправки.
*/
void Dispatcher_SendUsbResponse(const char* message)
{
// Используем локальный буфер для копирования сообщения.
// Это критически важно для потокобезопасности:
// `message` может указывать на временный буфер другой задачи,
// содержимое которого может измениться, пока FreeRTOS
// помещает его в очередь. Копирование гарантирует, что в очередь
// попадет стабильная, полная строка.
     char response_buffer[APP_USB_RESP_MAX_LEN];
     strncpy(response_buffer, message, APP_USB_RESP_MAX_LEN - 1);
     response_buffer[APP_USB_RESP_MAX_LEN - 1] = '\0'; // Гарантируем null-termination

     // Отправляем скопированное сообщение в очередь на передачу по USB.
     // Если очередь полна, ждем максимум 100 мс. Это предотвращает
     // блокировку всей задачи диспетчера, если USB-передача застряла.
     xQueueSend(usb_tx_queue_handle, (void *)response_buffer, pdMS_TO_TICKS(100));
}

// Вспомогательная функция для расчета CRC
static uint8_t calculate_crc(const uint8_t* data, uint16_t length)
{
	uint8_t crc = 0;
	for (uint16_t i = 0; i < length; i++) {
		crc ^= data[i];
		}
	return crc;
	}

// Вспомогательная функция для отправки бинарных данных
static void send_binary_packet(uint8_t* packet_data, uint16_t length)
{
	// Пока что мы отправляем бинарные данные через ту же очередь, что и строки.
	// Это вызовет проблемы в task_usb_handler, но позволит нам скомпилировать код.
	// На следующих шагах мы исправим и task_usb_handler.
	xQueueSend(usb_tx_queue_handle, (void *)packet_data, pdMS_TO_TICKS(100));
	}


void Dispatcher_SendAck(uint16_t command_code)
{
	// Структура ответа: Header(3) + Length(2) + Cmd(2) + Type(1) + Status(2) + CRC(1) = 11 байт
	uint8_t ack_packet[11];
	// 1. Header
	ack_packet[0] = 0x43; // 'C'
	ack_packet[1] = 0x4D; // 'M'
	ack_packet[2] = 0x3E; // '>'

	// 2. Length (Cmd + Type + Status + CRC = 2 + 1 + 2 + 1 = 6 байт)
	ack_packet[3] = 0x00;
	ack_packet[4] = 0x06;

	// 3. Command Code (Big-endian)
	ack_packet[5] = (command_code >> 8) & 0xFF;
	ack_packet[6] = command_code & 0xFF;

	// 4. Response Type (0x01 = ACK)
	ack_packet[7] = 0x01;

	// 5. Status (0x0000 = OK)
	ack_packet[8] = 0x00;
	ack_packet[9] = 0x00;

	// 6. CRC (с 5-го по 9-й байт включительно)
	ack_packet[10] = calculate_crc(&ack_packet[5], 5);
	send_binary_packet(ack_packet, sizeof(ack_packet));
}


void Dispatcher_SendNack(uint16_t command_code, uint16_t error_code)
{
	// Структура идентична ACK
	uint8_t nack_packet[11];

	// 1. Header
	nack_packet[0] = 0x43; nack_packet[1] = 0x4D; nack_packet[2] = 0x3E;

	// 2. Length
	nack_packet[3] = 0x00; nack_packet[4] = 0x06;

	// 3. Command Code
	nack_packet[5] = (command_code >> 8) & 0xFF;
	nack_packet[6] = command_code & 0xFF;

	// 4. Response Type (0x04 = ERROR)
	nack_packet[7] = 0x04;

	// 5. Status (Код ошибки)
	nack_packet[8] = (error_code >> 8) & 0xFF;
	nack_packet[9] = error_code & 0xFF;

	// 6. CRC
	nack_packet[10] = calculate_crc(&nack_packet[5], 5);
	send_binary_packet(nack_packet, sizeof(nack_packet));
}






















