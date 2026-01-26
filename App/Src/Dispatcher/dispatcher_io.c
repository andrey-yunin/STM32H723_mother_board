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
#include <stdbool.h>


// Вспомогательная функция для отправки пакета в очередь
static void send_packet_to_queue(const void* data, uint16_t length, bool is_string)
{
	USB_TxPacket_t tx_packet;
	// Если это строка, и она слишком длинная, обрезаем ее
	if (is_string && length >= APP_USB_RESP_MAX_LEN) {
		length = APP_USB_RESP_MAX_LEN - 1;
		}

	 tx_packet.length = length;
	 memcpy(tx_packet.data, data, length);

	 // Если это строка, гарантируем null-termination
	 if (is_string) {
		 tx_packet.data[length] = '\0';
		 }

	 xQueueSend(usb_tx_queue_handle, &tx_packet, pdMS_TO_TICKS(100));
	 }

void Dispatcher_SendUsbResponse(const char* message)
{
	send_packet_to_queue(message, strlen(message), true);
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

void Dispatcher_SendAck(uint16_t command_code)
{
	uint8_t ack_packet[11];
	ack_packet[0] = 0x43; ack_packet[1] = 0x4D; ack_packet[2] = 0x3E; // Header
	ack_packet[3] = 0x00; ack_packet[4] = 0x06; // Length
	ack_packet[5] = (command_code >> 8) & 0xFF; // Cmd
	ack_packet[6] = command_code & 0xFF;
	ack_packet[7] = 0x01; // Type ACK
	ack_packet[8] = 0x00; ack_packet[9] = 0x00; // Status OK
	ack_packet[10] = calculate_crc(&ack_packet[5], 5); // CRC

	send_packet_to_queue(ack_packet, sizeof(ack_packet), false);
	}

void Dispatcher_SendNack(uint16_t command_code, uint16_t error_code)
{
	uint8_t nack_packet[11];
	nack_packet[0] = 0x43; nack_packet[1] = 0x4D; nack_packet[2] = 0x3E; // Header
	nack_packet[3] = 0x00; nack_packet[4] = 0x06; // Length
	nack_packet[5] = (command_code >> 8) & 0xFF; // Cmd
	nack_packet[6] = command_code & 0xFF;
	nack_packet[7] = 0x04; // Type ERROR
	nack_packet[8] = (error_code >> 8) & 0xFF; // Status
	nack_packet[9] = error_code & 0xFF;
	nack_packet[10] = calculate_crc(&nack_packet[5], 5); // CRC
	send_packet_to_queue(nack_packet, sizeof(nack_packet), false);
}

void Dispatcher_SendDone(uint16_t command_code, uint16_t status)
{
	// Структура идентична ACK/NACK
	uint8_t done_packet[11];

	// 1. Header
	done_packet[0] = 0x43; done_packet[1] = 0x4D; done_packet[2] = 0x3E;

	// 2. Length (Cmd + Type + Status + CRC = 6 байт)
	done_packet[3] = 0x00; done_packet[4] = 0x06;

	// 3. Command Code
	done_packet[5] = (command_code >> 8) & 0xFF;
	done_packet[6] = command_code & 0xFF;

	// 4. Response Type (0x02 = DONE)
	done_packet[7] = 0x02;

	// 5. Status (0x0000 = OK, но могут быть и другие коды успеха)
	done_packet[8] = (status >> 8) & 0xFF;
	done_packet[9] = status & 0xFF;

	// 6. CRC
	done_packet[10] = calculate_crc(&done_packet[5], 5);
	send_packet_to_queue(done_packet, sizeof(done_packet), false);
	}

void Dispatcher_SendError(uint16_t command_code, uint16_t error_code)
{
	// Эта функция будет дублировать NACK, но с типом ответа "ERROR" (0x04).
	// По протоколу, NACK и ERROR могут иметь разный семантический смысл:
	// NACK - ошибка в самом пакете (CRC, неверный формат).
	// ERROR - ошибка выполнения самой команды на уровне логики.

	uint8_t error_packet[11];

	// 1. Header
	error_packet[0] = 0x43; error_packet[1] = 0x4D; error_packet[2] = 0x3E;

	// 2. Length
	error_packet[3] = 0x00; error_packet[4] = 0x06;

	// 3. Command Code
	error_packet[5] = (command_code >> 8) & 0xFF;
	error_packet[6] = command_code & 0xFF;

	// 4. Response Type (0x04 = ERROR)
	error_packet[7] = 0x04;

	// 5. Status (Код ошибки из errors.md)
	error_packet[8] = (error_code >> 8) & 0xFF;
	error_packet[9] = error_code & 0xFF;

	// 6. CRC
	error_packet[10] = calculate_crc(&error_packet[5], 5);
	send_packet_to_queue(error_packet, sizeof(error_packet), false);

}


void Dispatcher_SendData(uint16_t command_code, uint8_t response_type, uint16_t status, const uint8_t* data, uint16_t data_len)
{
    // Total payload length: Cmd Code (2) + Type (1) + Status (2) + Data (data_len) + CRC (1)
	uint16_t payload_segment_len_for_crc = 2 + 1 + 2 + data_len; // Command Code + Type + Status + Data
	uint16_t payload_len = payload_segment_len_for_crc + 1; // Add 1 byte for CRC

	uint16_t total_packet_len = 3 + 2 + payload_len; // Header (3) + Length Field (2) + Payload

	if (total_packet_len > APP_USB_RESP_MAX_LEN) {
		return; // Silently drop if packet is too long
		}

	uint8_t data_packet[APP_USB_RESP_MAX_LEN]; // Local buffer to construct the packet

	// 1. Header
	data_packet[0] = 0x43; data_packet[1] = 0x4D; data_packet[2] = 0x3E; // "CM>"

	// 2. Length (Payload Length)
	data_packet[3] = (uint8_t)(payload_len >> 8);
	data_packet[4] = (uint8_t)(payload_len & 0xFF);

	// 3. Command Code
	data_packet[5] = (uint8_t)(command_code >> 8);
	data_packet[6] = (uint8_t)(command_code & 0xFF);

	// 4. Response Type (e.g., 0x03 for DATA)
    data_packet[7] = response_type;

	// 5. Status
    data_packet[8] = (uint8_t)(status >> 8);
    data_packet[9] = (uint8_t)(status & 0xFF);

	// 6. Actual Data (copied after Status bytes)
	if (data_len > 0 && data != NULL) {
		memcpy(&data_packet[10], data, data_len);
	}

	// 7. CRC (Calculated from Command Code to end of Actual Data)
	uint8_t crc = calculate_crc(&data_packet[5], payload_segment_len_for_crc); // CRC over CmdCode, Type, Status, Data
	data_packet[10 + data_len] = crc;
	
	// Send the completely formed packet to the queue
	send_packet_to_queue(data_packet, total_packet_len, false); // false for binary
	}
























