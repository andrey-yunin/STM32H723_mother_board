/*
 * dispatcher_io.c
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#include "Dispatcher/dispatcher_io.h"
#include "shared_resources.h" // Для доступа к usb_tx_queue_handle
#include "app_config.h"       // Для APP_USB_RESP_MAX_LEN
#include "task_logger.h"
#include <string.h>           // Для strncpy
#include "cmsis_os.h"         // Для pdMS_TO_TICKS
#include <stdint.h> // Для uint8_t, uint16_t
#include <stdbool.h>

#define USB_TX_QUEUE_TIMEOUT_MS             100U

#define HOST_PACKET_HEADER_0                ((uint8_t)'C')
#define HOST_PACKET_HEADER_1                ((uint8_t)'M')
#define HOST_PACKET_HEADER_2                ((uint8_t)'>')

#define HOST_PACKET_HEADER_LEN              3U
#define HOST_PACKET_LENGTH_FIELD_LEN        2U
#define HOST_PACKET_COMMAND_CODE_LEN        2U
#define HOST_PACKET_RESPONSE_TYPE_LEN       1U
#define HOST_PACKET_STATUS_LEN              2U
#define HOST_PACKET_CRC_LEN                 1U

#define HOST_PACKET_LENGTH_OFFSET           HOST_PACKET_HEADER_LEN
#define HOST_PACKET_PAYLOAD_OFFSET          (HOST_PACKET_HEADER_LEN + HOST_PACKET_LENGTH_FIELD_LEN)
#define HOST_PACKET_COMMAND_OFFSET          HOST_PACKET_PAYLOAD_OFFSET
#define HOST_PACKET_RESPONSE_TYPE_OFFSET    (HOST_PACKET_COMMAND_OFFSET + HOST_PACKET_COMMAND_CODE_LEN)
#define HOST_PACKET_STATUS_OFFSET           (HOST_PACKET_RESPONSE_TYPE_OFFSET + HOST_PACKET_RESPONSE_TYPE_LEN)
#define HOST_PACKET_DATA_OFFSET             (HOST_PACKET_STATUS_OFFSET + HOST_PACKET_STATUS_LEN)

#define HOST_FIXED_RESPONSE_CRC_INPUT_LEN   (HOST_PACKET_COMMAND_CODE_LEN + HOST_PACKET_RESPONSE_TYPE_LEN + HOST_PACKET_STATUS_LEN)
#define HOST_FIXED_RESPONSE_PAYLOAD_LEN     (HOST_FIXED_RESPONSE_CRC_INPUT_LEN + HOST_PACKET_CRC_LEN)
#define HOST_FIXED_RESPONSE_PACKET_LEN      (HOST_PACKET_HEADER_LEN + HOST_PACKET_LENGTH_FIELD_LEN + HOST_FIXED_RESPONSE_PAYLOAD_LEN)
#define HOST_FIXED_RESPONSE_CRC_OFFSET      (HOST_PACKET_PAYLOAD_OFFSET + HOST_FIXED_RESPONSE_CRC_INPUT_LEN)

#define HOST_DATA_RESPONSE_CRC_INPUT_LEN(data_len) \
		(HOST_FIXED_RESPONSE_CRC_INPUT_LEN + (data_len))
#define HOST_DATA_RESPONSE_PAYLOAD_LEN(data_len) \
		(HOST_DATA_RESPONSE_CRC_INPUT_LEN(data_len) + HOST_PACKET_CRC_LEN)
#define HOST_DATA_RESPONSE_PACKET_LEN(data_len) \
		(HOST_PACKET_HEADER_LEN + HOST_PACKET_LENGTH_FIELD_LEN + HOST_DATA_RESPONSE_PAYLOAD_LEN(data_len))

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

	xQueueSend(usb_tx_queue_handle, &tx_packet, pdMS_TO_TICKS(USB_TX_QUEUE_TIMEOUT_MS));
}

void Dispatcher_SendUsbResponse(const char* message)
{
	(void)Logger_LogText(message);
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

static void write_u16_be(uint8_t* dst, uint16_t value)
{
	dst[0] = (uint8_t)(value >> 8);
	dst[1] = (uint8_t)(value & 0xFFU);
}

static void write_host_header(uint8_t* packet)
{
	packet[0] = HOST_PACKET_HEADER_0;
	packet[1] = HOST_PACKET_HEADER_1;
	packet[2] = HOST_PACKET_HEADER_2;
}

static void send_fixed_host_response(uint16_t command_code,
		HostResponseType_t response_type,
		uint16_t status)
{
	uint8_t packet[HOST_FIXED_RESPONSE_PACKET_LEN];

	write_host_header(packet);
	write_u16_be(&packet[HOST_PACKET_LENGTH_OFFSET], HOST_FIXED_RESPONSE_PAYLOAD_LEN);
	write_u16_be(&packet[HOST_PACKET_COMMAND_OFFSET], command_code);
	packet[HOST_PACKET_RESPONSE_TYPE_OFFSET] = (uint8_t)response_type;
	write_u16_be(&packet[HOST_PACKET_STATUS_OFFSET], status);
	packet[HOST_FIXED_RESPONSE_CRC_OFFSET] =
			calculate_crc(&packet[HOST_PACKET_COMMAND_OFFSET], HOST_FIXED_RESPONSE_CRC_INPUT_LEN);

	send_packet_to_queue(packet, sizeof(packet), false);
}

void Dispatcher_SendAck(uint16_t command_code)
{
	send_fixed_host_response(command_code, HOST_RESPONSE_TYPE_ACK, HOST_STATUS_OK);
}

void Dispatcher_SendNack(uint16_t command_code, uint16_t error_code)
{
	send_fixed_host_response(command_code, HOST_RESPONSE_TYPE_NACK, error_code);
}

void Dispatcher_SendDone(uint16_t command_code, uint16_t status)
{
	send_fixed_host_response(command_code, HOST_RESPONSE_TYPE_DONE, status);
}

void Dispatcher_SendError(uint16_t command_code, uint16_t error_code)
{
	send_fixed_host_response(command_code, HOST_RESPONSE_TYPE_ERROR, error_code);
}


void Dispatcher_SendData(uint16_t command_code,
		HostResponseType_t response_type,
		uint16_t status,
		const uint8_t* data,
		uint16_t data_len)
{
	uint16_t crc_input_len = HOST_DATA_RESPONSE_CRC_INPUT_LEN(data_len);
	uint16_t payload_len = HOST_DATA_RESPONSE_PAYLOAD_LEN(data_len);
	uint16_t total_packet_len = HOST_DATA_RESPONSE_PACKET_LEN(data_len);

	if (total_packet_len > APP_USB_RESP_MAX_LEN) {
		return; // Silently drop if packet is too long
	}

	uint8_t data_packet[APP_USB_RESP_MAX_LEN]; // Local buffer to construct the packet

	write_host_header(data_packet);
	write_u16_be(&data_packet[HOST_PACKET_LENGTH_OFFSET], payload_len);
	write_u16_be(&data_packet[HOST_PACKET_COMMAND_OFFSET], command_code);
	data_packet[HOST_PACKET_RESPONSE_TYPE_OFFSET] = (uint8_t)response_type;
	write_u16_be(&data_packet[HOST_PACKET_STATUS_OFFSET], status);

	// 6. Actual Data (copied after Status bytes)
	if (data_len > 0 && data != NULL) {
		memcpy(&data_packet[HOST_PACKET_DATA_OFFSET], data, data_len);
	}

	uint8_t crc = calculate_crc(&data_packet[HOST_PACKET_COMMAND_OFFSET], crc_input_len);
	data_packet[HOST_PACKET_DATA_OFFSET + data_len] = crc;

	// Send the completely formed packet to the queue
	send_packet_to_queue(data_packet, total_packet_len, false); // false for binary
}


