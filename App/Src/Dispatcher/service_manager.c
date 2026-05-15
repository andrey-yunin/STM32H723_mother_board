/*
 * service_manager.c
 *
 *  Created on: Apr 13, 2026
 *      Author: andrey
 */

#include "Dispatcher/service_manager.h"
#include "Dispatcher/can_packer.h"
#include "shared_resources.h" // Для can_tx_queue_handle
#include "main.h"             // Для HAL_GetTick()
#include "Dispatcher/device_mapping.h"
#include <string.h>
#include "Dispatcher/can_response_router.h"


static DeviceNode_t g_inventory[MAX_DISCOVERED_NODES];
static uint8_t g_nodes_count = 0;

void ServiceManager_Init(void) {
	memset(g_inventory, 0, sizeof(g_inventory));
	g_nodes_count = 0;
}

/**
 * @brief Запуск широковещательного сканирования шины.
 */
void ServiceManager_StartDiscovery(void)
{
	CAN_Message_t msg;
	// Сбрасываем статус онлайн перед сканированием
    for(int i = 0; i < MAX_DISCOVERED_NODES; i++) g_inventory[i].is_online = false;

    // Рассылаем GetInfo на Broadcast (NodeID 0x00)
    Packer_CreateGetInfoMsg(CAN_ADDR_BROADCAST, &msg);

    /*
     * Broadcast discovery отвечает с реальных source NodeID.
     * Поэтому router не держит route на broadcast-адресе: service-route
     * откроется по ACK F001 от каждого конкретного узла.
     */
    CanResponseRouter_Register(TX_OWNER_SERVICE_INTERNAL,
                                 CAN_ADDR_BROADCAST,
                                 CAN_CMD_SRV_GET_INFO,
                                 0,
                                 false,
                                 0,
                                 0);

    xQueueSend(can_tx_queue_handle, &msg, 0);
}

/**
 * @brief Обновление данных об узле при получении ответа DATA (0xF001)
 */
void ServiceManager_UpdateNode(const CAN_Response_t* res)
{
	if (res == NULL || res->command_code != CAN_CMD_SRV_GET_INFO) return;
	DeviceNode_t* node = NULL;

	// 1. Ищем, есть ли уже такой узел в инвентаре
	for (int i = 0; i < g_nodes_count; i++) {
		if (g_inventory[i].node_id == res->source_addr)
		{
			node = &g_inventory[i];
			break;
			}
		}

	// 2. Если узел новый — добавляем в список
	if (node == NULL && g_nodes_count < MAX_DISCOVERED_NODES) {
		node = &g_inventory[g_nodes_count++];
		node->node_id = res->source_addr;
		}

	// 3. Заполняем данные из Payload [Type][Maj][Min][HW]
	if (node != NULL) {
		node->device_type = res->payload.raw[0];
		node->fw_ver[0]   = res->payload.raw[1];
		node->fw_ver[1]   = res->payload.raw[2];
		node->is_online   = true;
		node->last_seen_ms = HAL_GetTick();
		}
	}

/**
 * @brief Проверка готовности физических узлов согласно маске INIT (0x1002)
 */
bool ServiceManager_CheckInventory(uint8_t modules_mask) {

	// 1. Спрашиваем у маппинга битовую маску необходимых NodeID (0x20 -> бит 0, 0x21 -> бит 1)
	uint32_t required_nodes_mask = DeviceMapping_GetRequiredNodesMask(modules_mask);

	// 2. Проверяем наличие и статус каждой требуемой платы
	for (int i = 0; i < 32; i++) {
		if (required_nodes_mask & (1 << i)) {
			uint8_t target_node = 0x20 + i;

			// Если плата 0x20+i нужна для работы, но её нет в сети — блокируем INIT
			if (!ServiceManager_IsNodeOnline(target_node)) {
				// Здесь можно добавить лог: "CRITICAL: Node 0x%02X is required but Offline"
				return false;
				}
			}
		}
	return true; // Все ресурсы на месте, можно начинать INIT

}


/**
 * @brief Вспомогательная функция проверки статуса конкретного узла
 */
bool ServiceManager_IsNodeOnline(uint8_t node_id) {
	for (int i = 0; i < g_nodes_count; i++) {
		if (g_inventory[i].node_id == node_id && g_inventory[i].is_online) {
			return true;
			}
		}
	return false;
}

void ServiceManager_Run(void)
{
	CanRoutedResponse_t routed;

	while (xQueueReceive(can_service_rx_queue_handle, &routed, 0) == pdPASS) {
		CAN_Response_t response = routed.parsed;
		uint16_t service_cmd = routed.context_command_code;

		/*
		 * Service DATA не несет универсальный command_code.
		 * Команду service transaction дает router context:
		 * ACK F001/F004/F007 -> DATA... -> DONE.
		 */
		if (response.msg_type == CAN_MSG_TYPE_DATA_DONE_LOG &&
			response.sub_type == CAN_SUB_TYPE_DATA) {

			switch (service_cmd) {
				case CAN_CMD_SRV_GET_INFO:
					/*
					 * F001 DATA format:
					 * payload.raw[0] = DeviceType
					 * payload.raw[1] = FW major
					 * payload.raw[2] = FW minor
					 * payload.raw[3] = channel count / hw info
					 * Остальные DATA кадры UID/строка версии обработаем
					 * в service-window блоке.
					 */
					if (!ServiceManager_IsNodeOnline(response.source_addr)) {
						response.command_code = CAN_CMD_SRV_GET_INFO;
						response.command_code_valid = true;
						ServiceManager_UpdateNode(&response);
					}
					break;

				case 0xF004:
					/* GET_UID будет обработан в следующем service-window блоке. */
					break;

				case 0xF007:
					/* GET_STATUS metrics будут обработаны в следующем service-window блоке. */
					break;

				default:
					break;
			}
		}
	}
}

