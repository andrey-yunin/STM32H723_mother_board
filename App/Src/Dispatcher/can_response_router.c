/*
 * can_response_router.c
 *
 *  Created on: May 14, 2026
 *      Author: andrey
 */

#include "Dispatcher/can_response_router.h"
#include "Dispatcher/can_packer.h"
#include "shared_resources.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define ROUTER_TRACKED_ROUTES 8U

typedef struct {
	bool active;
	CanTxOwner_t owner;
	uint8_t node_id;
	uint16_t command_code;
	uint8_t channel;
	bool channel_valid;
	uint32_t job_id;
	uint16_t host_command_code;
	CanRxRoute_t route;
} ActiveRoute_t;

/*
 * ВАЖНО О ПОТОКОБЕЗОПАСНОСТИ:
 *
 * g_active_routes является внутренним runtime-состоянием router-а.
 * Таблицу меняют:
 * - CanResponseRouter_Register() при отправке low-level команды;
 * - CanResponseRouter_Run() при ACK/DONE/NACK;
 * - CanResponseRouter_CloseJob() при timeout/error/reset job-а.
 *
 * Эти вызовы могут приходить из разных task context, поэтому все обращения
 * к таблице защищены короткими FreeRTOS critical sections.
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

static CanRxRoute_t Router_RouteForOwner(CanTxOwner_t owner)
{
	return (owner == TX_OWNER_SERVICE_INTERNAL)
			? CAN_RX_ROUTE_SERVICE_INTERNAL
			: CAN_RX_ROUTE_HOST_OPERATION;
}

static CanTxOwner_t Router_OwnerForCommand(uint16_t command_code)
{
	return Router_IsServiceCommand(command_code)
			? TX_OWNER_SERVICE_INTERNAL
			: TX_OWNER_HOST_OPERATION;
}

static bool Router_CommandMatches(const ActiveRoute_t* route,
                                  uint8_t node_id,
                                  uint16_t command_code)
{
	return route != NULL &&
			route->active &&
			route->node_id == node_id &&
			route->command_code == command_code;
}

static ActiveRoute_t* Router_FindFreeRoute(void)
{
	for (uint8_t i = 0; i < ROUTER_TRACKED_ROUTES; i++) {
		if (!g_active_routes[i].active) {
			return &g_active_routes[i];
			}
	}

	return NULL;
}

static ActiveRoute_t* Router_FindFirstByNodeCommand(uint8_t node_id,
                                                    uint16_t command_code)
{
	for (uint8_t i = 0; i < ROUTER_TRACKED_ROUTES; i++) {
		if (Router_CommandMatches(&g_active_routes[i], node_id, command_code)) {
			return &g_active_routes[i];
			}
		}

	return NULL;
}

static ActiveRoute_t* Router_FindExactRoute(uint8_t node_id,
                                            uint16_t command_code,
                                            uint8_t channel,
                                            bool channel_valid)
{
	ActiveRoute_t* command_level_route = NULL;

	for (uint8_t i = 0; i < ROUTER_TRACKED_ROUTES; i++) {
		ActiveRoute_t* route = &g_active_routes[i];

		if (!Router_CommandMatches(route, node_id, command_code)) {
			continue;
			}

		if (route->channel_valid && channel_valid) {
			if (route->channel == channel) {
				return route;
				}
			continue;
			}

		if (!route->channel_valid) {
			command_level_route = route;
			}
	}

	return command_level_route;
}

static ActiveRoute_t* Router_FindUniqueByNode(uint8_t node_id, bool* ambiguous)
{
	ActiveRoute_t* found = NULL;

	if (ambiguous != NULL) {
		*ambiguous = false;
		}

	for (uint8_t i = 0; i < ROUTER_TRACKED_ROUTES; i++) {
		ActiveRoute_t* route = &g_active_routes[i];

		if (!route->active || route->node_id != node_id) {
			continue;
			}

		if (found != NULL) {
			if (ambiguous != NULL) {
				*ambiguous = true;
				}
			return NULL;
			}

		found = route;
		}

	return found;
}

static ActiveRoute_t* Router_FindUniqueByNodeChannel(uint8_t node_id,
                                                     uint8_t channel,
                                                     bool* ambiguous)
{
	ActiveRoute_t* found = NULL;

	if (ambiguous != NULL) {
		*ambiguous = false;
		}

	for (uint8_t i = 0; i < ROUTER_TRACKED_ROUTES; i++) {
		ActiveRoute_t* route = &g_active_routes[i];

		if (!route->active ||
				route->node_id != node_id ||
				!route->channel_valid ||
				route->channel != channel) {
			continue;
			}

		if (found != NULL) {
			if (ambiguous != NULL) {
				*ambiguous = true;
				}
			return NULL;
			}

		found = route;
		}

	return found;
}

static ActiveRoute_t* Router_UpsertRoute(CanTxOwner_t owner,
                                         uint8_t node_id,
                                         uint16_t command_code,
                                         uint8_t channel,
                                         bool channel_valid,
                                         uint32_t job_id,
                                         uint16_t host_command_code)
{
	ActiveRoute_t* route = Router_FindExactRoute(node_id,
	                                             command_code,
	                                             channel,
	                                             channel_valid);

	if (route == NULL) {
		route = Router_FindFreeRoute();
		}

	if (route != NULL) {
		memset(route, 0, sizeof(*route));
		route->active = true;
		route->owner = owner;
		route->node_id = node_id;
		route->command_code = command_code;
		route->channel = channel;
		route->channel_valid = channel_valid;
		route->job_id = job_id;
		route->host_command_code = host_command_code;
		route->route = Router_RouteForOwner(owner);
		}

	return route;
}

static void Router_CloseCommandRoutes(uint8_t node_id, uint16_t command_code)
{
	for (uint8_t i = 0; i < ROUTER_TRACKED_ROUTES; i++) {
		if (Router_CommandMatches(&g_active_routes[i], node_id, command_code)) {
			memset(&g_active_routes[i], 0, sizeof(g_active_routes[i]));
			}
		}
}

static void Router_CloseDoneRoute(uint8_t node_id,
                                  uint16_t command_code,
                                  uint8_t channel)
{
	for (uint8_t i = 0; i < ROUTER_TRACKED_ROUTES; i++) {
		ActiveRoute_t* route = &g_active_routes[i];

		if (!Router_CommandMatches(route, node_id, command_code)) {
			continue;
			}

		if (!route->channel_valid || route->channel == channel) {
			memset(route, 0, sizeof(*route));
			return;
			}
		}
}

static void Router_CopyContextFromRoute(const ActiveRoute_t* route,
                                        CanRoutedResponse_t* routed)
{
	if (route == NULL || routed == NULL) {
		return;
		}

	routed->route = route->route;
	routed->context_valid = true;
	routed->context_owner = route->owner;
	routed->context_command_code = route->command_code;
	routed->context_channel = route->channel;
	routed->context_channel_valid = route->channel_valid;
	routed->context_job_id = route->job_id;
	routed->context_host_command_code = route->host_command_code;
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
	/*
	 * Broadcast service discovery получает ответы от реальных source NodeID.
	 * Для него route создается по ACK каждого узла, а не по broadcast-адресу.
	 */
	if (node_id == CAN_ADDR_BROADCAST && !channel_valid) {
		return true;
		}

	taskENTER_CRITICAL();
	ActiveRoute_t* route = Router_UpsertRoute(owner,
	                                           node_id,
	                                           command_code,
	                                           channel,
	                                           channel_valid,
	                                           job_id,
	                                           host_command_code);
	taskEXIT_CRITICAL();

	return route != NULL;
}

void CanResponseRouter_CloseJob(uint32_t job_id)
{
	if (job_id == 0U) {
		return;
		}

	taskENTER_CRITICAL();
	for (uint8_t i = 0; i < ROUTER_TRACKED_ROUTES; i++) {
		ActiveRoute_t* route = &g_active_routes[i];

		if (route->active &&
				route->owner == TX_OWNER_HOST_OPERATION &&
				route->job_id == job_id) {
			memset(route, 0, sizeof(*route));
			}
		}
	taskEXIT_CRITICAL();
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
		routed.context_owner = Router_OwnerForCommand(response.command_code);

		routed.route = CAN_RX_ROUTE_HOST_OPERATION;
		routed.context_command_code = response.command_code;
		routed.context_valid = response.command_code_valid;

		taskENTER_CRITICAL();
		if (response.msg_type == CAN_MSG_TYPE_ACK) {
			ActiveRoute_t* active = Router_FindFirstByNodeCommand(response.source_addr,
			                                                      response.command_code);

			if (active == NULL) {
				CanTxOwner_t owner = Router_OwnerForCommand(response.command_code);
				active = Router_UpsertRoute(owner,
				                            response.source_addr,
				                            response.command_code,
				                            0,
				                            false,
				                            0,
				                            0);
				}

			/*
			 * ACK не несет channel. Если route уже был зарегистрирован отправителем
			 * с channel context, сохраняем этот context; иначе открываем command-level
			 * route для service/discovery или legacy sender-ов.
			 */
			if (active != NULL) {
				Router_CopyContextFromRoute(active, &routed);
				}
			else {
				routed.route = Router_RouteForOwner(Router_OwnerForCommand(response.command_code));
				routed.context_valid = true;
				routed.context_command_code = response.command_code;
				}
		}
		else if (response.msg_type == CAN_MSG_TYPE_NACK) {
			ActiveRoute_t* active = Router_FindFirstByNodeCommand(response.source_addr,
			                                                      response.command_code);

			if (active != NULL) {
				Router_CopyContextFromRoute(active, &routed);
			}
			else {
				routed.route = Router_RouteForOwner(Router_OwnerForCommand(response.command_code));
				routed.context_valid = true;
				routed.context_owner = Router_OwnerForCommand(response.command_code);
				routed.context_command_code = response.command_code;
			}

			/*
			 * NACK не несет channel. Это command-level отказ, поэтому закрываем
			 * все active routes с тем же node + command.
			 */
			Router_CloseCommandRoutes(response.source_addr, response.command_code);
		}
		else if (response.msg_type == CAN_MSG_TYPE_DATA_DONE_LOG) {
			if (response.sub_type == CAN_SUB_TYPE_DATA) {
				bool ambiguous = false;
				ActiveRoute_t* active = NULL;

				/*
				 * DATA не несет универсальный command_code. Сначала пробуем
				 * найти unique route по node + channel, если payload[0] в этом
				 * домене является channel/sensor index. Если это не помогло,
				 * принимаем DATA только когда у node есть ровно один active route.
				 */
				if (response.data_len > 0U) {
					active = Router_FindUniqueByNodeChannel(response.source_addr,
					                                       response.ch_idx,
					                                       &ambiguous);
					}

				if (active == NULL && !ambiguous) {
					active = Router_FindUniqueByNode(response.source_addr, &ambiguous);
					}

				if (active != NULL && !ambiguous) {
					Router_CopyContextFromRoute(active, &routed);
				}
				else {
					/*
					 * DATA без однозначного context не должен продвигать
					 * service или job.
					 */
					routed.route = CAN_RX_ROUTE_HOST_OPERATION;
					routed.context_valid = false;
					routed.context_command_code = 0;
				}
			}
			else if (response.sub_type == CAN_SUB_TYPE_DONE) {
				ActiveRoute_t* active = Router_FindExactRoute(response.source_addr,
				                                              response.command_code,
				                                              response.ch_idx,
				                                              true);

				if (active == NULL) {
					active = Router_FindFirstByNodeCommand(response.source_addr,
					                                      response.command_code);
					}

				if (active != NULL) {
					Router_CopyContextFromRoute(active, &routed);
				}
				else {
					routed.route = Router_RouteForOwner(Router_OwnerForCommand(response.command_code));
					routed.context_valid = true;
					routed.context_owner = Router_OwnerForCommand(response.command_code);
					routed.context_command_code = response.command_code;
				}

				routed.context_channel = response.ch_idx;
				routed.context_channel_valid = true;

				/*
				 * DONE несет channel, поэтому закрывает только свою transaction.
				 */
				Router_CloseDoneRoute(response.source_addr,
				                      response.command_code,
				                      response.ch_idx);
			}
			else {
				bool ambiguous = false;
				ActiveRoute_t* active = Router_FindUniqueByNode(response.source_addr,
				                                                &ambiguous);

				if (active != NULL && !ambiguous) {
					Router_CopyContextFromRoute(active, &routed);
				}
			}
		}
		taskEXIT_CRITICAL();

		QueueHandle_t target_queue =
			(routed.route == CAN_RX_ROUTE_SERVICE_INTERNAL)
				? can_service_rx_queue_handle
				: can_job_rx_queue_handle;

		(void)xQueueSend(target_queue, &routed, 0);
	}
}
