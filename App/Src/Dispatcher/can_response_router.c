/*
 * can_response_router.c
 *
 *  Created on: May 14, 2026
 *      Author: andrey
 */

#include "Dispatcher/can_response_router.h"
#include "Dispatcher/can_packer.h"
#include "shared_resources.h"
#include <string.h>

#define ROUTER_TRACKED_ROUTES 8U

typedef struct {
	bool active;
	uint8_t node_id;
	uint16_t command_code;
	CanRxRoute_t route;
} ActiveRoute_t;

/*
 * ВАЖНО О ПОТОКОБЕЗОПАСНОСТИ:
 *
 * g_active_routes является внутренним runtime-состоянием router-а.
 * На этом этапе таблицу меняет только CanResponseRouter_Run() из одного task context.
 *
 * Если в будущем CanResponseRouter_Register() начнет изменять эту таблицу
 * из ServiceManager/JobManager или другой задачи, нужна синхронизация:
 * mutex/critical section либо route-registration queue.
 */
static ActiveRoute_t g_active_routes[ROUTER_TRACKED_ROUTES];

static bool Router_IsServiceCommand(uint16_t command_code)
{
	switch (command_code) {
		case CAN_CMD_SRV_GET_INFO:
		case CAN_CMD_SRV_REBOOT:
		case CAN_CMD_SRV_COMMIT:
		case CAN_CMD_SRV_SET_NODE_ID:
		case CAN_CMD_SRV_FACTORY_RESET:
		case CAN_CMD_SRV_SCAN_1WIRE:
		case 0xF004U: // GET_UID, TODO: заменить на CAN_CMD_SRV_GET_UID
		case 0xF007U: // GET_STATUS, TODO: заменить на CAN_CMD_SRV_GET_STATUS
			return true;

		default:
			return false;
	}
}

static ActiveRoute_t* Router_FindRoute(uint8_t node_id)
{
	for (uint8_t i = 0; i < ROUTER_TRACKED_ROUTES; i++) {
		if (g_active_routes[i].active &&
			g_active_routes[i].node_id == node_id) {
			return &g_active_routes[i];
		}
	}

	return NULL;
}

static void Router_OpenRoute(uint8_t node_id,
                             uint16_t command_code,
                             CanRxRoute_t route_type)
{
	ActiveRoute_t* route = Router_FindRoute(node_id);

	if (route == NULL) {
		for (uint8_t i = 0; i < ROUTER_TRACKED_ROUTES; i++) {
			if (!g_active_routes[i].active) {
				route = &g_active_routes[i];
				break;
			}
		}
	}

	if (route != NULL) {
		route->active = true;
		route->node_id = node_id;
		route->command_code = command_code;
		route->route = route_type;
	}
}

static void Router_CloseRoute(uint8_t node_id, uint16_t command_code)
{
	ActiveRoute_t* route = Router_FindRoute(node_id);

	if (route != NULL && route->command_code == command_code) {
		memset(route, 0, sizeof(*route));
	}
}

void CanResponseRouter_Init(void)
{
	memset(g_active_routes, 0, sizeof(g_active_routes));
}

bool CanResponseRouter_Register(CanTxOwner_t owner,
                                uint8_t node_id,
                                uint16_t command_code,
                                uint8_t channel,
                                bool channel_valid,
                                uint32_t job_id,
                                uint16_t host_command_code)
{
	(void)owner;
	(void)node_id;
	(void)command_code;
	(void)channel;
	(void)channel_valid;
	(void)job_id;
	(void)host_command_code;

	/*
	 * Первый этап: внешний Register не пишет в route table.
	 * Route открывается по фактическому ACK от Executor.
	 */
	return true;
}

void CanResponseRouter_Run(void)
{
	CAN_Message_t rx_msg;

	while (xQueueReceive(can_rx_queue_handle, &rx_msg, 0) == pdPASS) {
		CAN_Response_t response;

		if (!Packer_ParseCanResponse(&rx_msg, &response)) {
			continue;
		}

		CanRoutedResponse_t routed;
		memset(&routed, 0, sizeof(routed));

		routed.raw = rx_msg;
		routed.parsed = response;
		routed.context_node_id = response.source_addr;

		CanRxRoute_t route_type = CAN_RX_ROUTE_HOST_OPERATION;
		uint16_t context_command_code = response.command_code;
		bool context_valid = response.command_code_valid;

		if (response.msg_type == CAN_MSG_TYPE_ACK) {
			route_type = Router_IsServiceCommand(response.command_code)
					   ? CAN_RX_ROUTE_SERVICE_INTERNAL
					   : CAN_RX_ROUTE_HOST_OPERATION;

			context_valid = true;
			context_command_code = response.command_code;

			/*
			 * ACK открывает active route для последующих DATA.
			 * DATA не несет универсальный command_code и наследует команду отсюда.
			 */
			Router_OpenRoute(response.source_addr,
							 response.command_code,
							 route_type);
		}
		else if (response.msg_type == CAN_MSG_TYPE_NACK) {
			ActiveRoute_t* active = Router_FindRoute(response.source_addr);

			if (active != NULL) {
				route_type = active->route;
			}
			else {
				route_type = Router_IsServiceCommand(response.command_code)
						   ? CAN_RX_ROUTE_SERVICE_INTERNAL
						   : CAN_RX_ROUTE_HOST_OPERATION;
			}

			context_valid = true;
			context_command_code = response.command_code;

			Router_CloseRoute(response.source_addr, response.command_code);
		}
		else if (response.msg_type == CAN_MSG_TYPE_DATA_DONE_LOG) {
			if (response.sub_type == CAN_SUB_TYPE_DATA) {
				ActiveRoute_t* active = Router_FindRoute(response.source_addr);

				if (active != NULL) {
					route_type = active->route;
					context_valid = true;
					context_command_code = active->command_code;
				}
				else {
					/*
					 * DATA без открытого ACK-контекста не должен продвигать
					 * service или job. На этом этапе оставляем его в Host path;
					 * следующий слой отфильтрует unexpected DATA по expected state.
					 */
					route_type = CAN_RX_ROUTE_HOST_OPERATION;
					context_valid = false;
					context_command_code = 0;
				}
			}
			else if (response.sub_type == CAN_SUB_TYPE_DONE) {
				ActiveRoute_t* active = Router_FindRoute(response.source_addr);

				if (active != NULL) {
					route_type = active->route;
				}
				else {
					route_type = Router_IsServiceCommand(response.command_code)
							   ? CAN_RX_ROUTE_SERVICE_INTERNAL
							   : CAN_RX_ROUTE_HOST_OPERATION;
				}

				context_valid = true;
				context_command_code = response.command_code;

				Router_CloseRoute(response.source_addr, response.command_code);
			}
			else {
				ActiveRoute_t* active = Router_FindRoute(response.source_addr);

				if (active != NULL) {
					route_type = active->route;
					context_valid = true;
					context_command_code = active->command_code;
				}
			}
		}

		routed.route = route_type;
		routed.context_valid = context_valid;
		routed.context_command_code = context_command_code;

		QueueHandle_t target_queue =
			(route_type == CAN_RX_ROUTE_SERVICE_INTERNAL)
				? can_service_rx_queue_handle
				: can_job_rx_queue_handle;

		(void)xQueueSend(target_queue, &routed, 0);
	}
}
