/*
 * can_response_router.h
 *
 *  Created on: May 14, 2026
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_CAN_RESPONSE_ROUTER_H_
#define INC_DISPATCHER_CAN_RESPONSE_ROUTER_H_

#include <stdbool.h>
#include <stdint.h>
#include "Dispatcher/can_packer.h"

typedef enum {
	TX_OWNER_HOST_OPERATION = 0,
	TX_OWNER_SERVICE_INTERNAL,
	TX_OWNER_SAFETY_OPERATION,
	TX_OWNER_HOST_DIRECT_OPERATION
} CanTxOwner_t;

typedef enum {
	CAN_RX_ROUTE_HOST_OPERATION = 0,
	CAN_RX_ROUTE_SERVICE_INTERNAL,
	CAN_RX_ROUTE_SAFETY_OPERATION,
	CAN_RX_ROUTE_HOST_DIRECT_OPERATION
} CanRxRoute_t;

/*
 * Ответ после маршрутизации.
 *
 * raw    - исходный CAN frame.
 * parsed - результат общего parser-а.
 *
 * context_* - контекст активной операции.
 * Для ACK/NACK/DONE command совпадает с parsed.command_code.
 * Для DATA command/channel берутся из active route, зарегистрированного
 * отправителем команды или открытого предыдущим ACK.
 */
typedef struct {
	CAN_Message_t raw;
	CAN_Response_t parsed;

	CanRxRoute_t route;

	bool context_valid;
	CanTxOwner_t context_owner;
	uint8_t context_node_id;
	uint16_t context_command_code;
	uint8_t context_channel;
	bool context_channel_valid;
	uint32_t context_job_id;
	uint16_t context_host_command_code;
} CanRoutedResponse_t;

void CanResponseRouter_Init(void);

bool CanResponseRouter_Register(CanTxOwner_t owner,
                                uint8_t node_id,
                                uint16_t command_code,
                                uint8_t channel,
                                bool channel_valid,
                                uint32_t job_id,
                                uint16_t host_command_code);

void CanResponseRouter_CloseJob(uint32_t job_id);
void CanResponseRouter_CloseOperation(CanTxOwner_t owner, uint32_t operation_id);

/*
 * Читает raw can_rx_queue_handle и раскладывает ответы в:
 * - can_job_rx_queue_handle: Host recipe/job operations;
 * - can_service_rx_queue_handle: service/internal F001/F004/F007.
 * - can_safety_rx_queue_handle: safety operations, включая 0x1010 EMERGENCY_STOP.
 * - can_host_direct_rx_queue_handle: executor-backed direct Host commands.
 */
void CanResponseRouter_Run(void);

#endif /* INC_DISPATCHER_CAN_RESPONSE_ROUTER_H_ */
