/*
 * service_manager.h
 *
 *  Created on: Apr 13, 2026
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_SERVICE_MANAGER_H_
#define INC_DISPATCHER_SERVICE_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>
#include "Dispatcher/can_packer.h"


#define MAX_DISCOVERED_NODES    8

/**
 * @brief Паспорт физического узла экосистемы DDS-240
 */
typedef struct {
	uint8_t  node_id;       // NodeID (0x20, 0x30, 0x40)
	uint8_t  device_type;   // Из ответа F001
    uint8_t  fw_ver[2];     // [Major, Minor]
    uint8_t  channel_count; // Количество физических каналов из F001
    uint32_t uid[3];        // STM32 Unique ID (96-bit)
    bool     is_online;
    uint32_t last_seen_ms;
} DeviceNode_t;

void ServiceManager_Init(void);

/**
 * @brief Запуск процесса Discovery (Broadcast GetInfo)
 */
void ServiceManager_StartDiscovery(void);

/**
 * @brief Запуск recovery discovery после executor timeout.
 * @param target_node_id NodeID исполнителя, который должен снова пройти
 *        discovery/UID/status перед возвратом системы в READY.
 */
void ServiceManager_StartRecovery(uint8_t target_node_id);

/**
 * @brief Обработка сервисного ответа от узла
 */
void ServiceManager_UpdateNode(const CAN_Response_t* res);

/**
 * @brief Проверка, все ли модули из маски присутствуют в сети
 * @param modules_mask Битовая маска (как в команде INIT)
 */
bool ServiceManager_CheckInventory(uint8_t modules_mask);

/**
 * @brief Проверяет, находится ли конкретный узел в сети и активен ли он.
 * @param node_id Физический адрес платы (напр. 0x20, 0x30, 0x40)
 */
bool ServiceManager_IsNodeOnline(uint8_t node_id);


void ServiceManager_Run(void);


#endif /* INC_DISPATCHER_SERVICE_MANAGER_H_ */
