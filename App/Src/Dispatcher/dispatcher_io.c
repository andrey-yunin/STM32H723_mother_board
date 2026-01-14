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






















