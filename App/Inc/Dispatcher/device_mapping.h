/*
 * device_mapping.h
 *
 * СЛОЙ ФИЗИЧЕСКИХ УСТРОЙСТВ (Conductor <-> Executor)
 * -------------------------------------------------
 * Описывает структуры физических адресов на шине CAN 
 * и предоставляет интерфейс для трансляции Системных ID.
 */

#ifndef INC_DISPATCHER_DEVICE_MAPPING_H_
#define INC_DISPATCHER_DEVICE_MAPPING_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Структура физического адреса исполнителя
 */
typedef struct {
    uint8_t node_id;    // CAN NodeID (0x20, 0x30, 0x40)
    uint8_t ch_idx;     // 0-based физический индекс канала на плате
    bool    is_valid;   // Флаг корректности маппинга
} DevicePhysAddr_t;

/**
 * @brief Трансляция Системного ID мотора в физический адрес
 */
DevicePhysAddr_t DeviceMapping_GetMotorPhysAddr(uint8_t system_id);


/**
 * @brief Трансляция Системного ID помпы в физический адрес
 */
DevicePhysAddr_t DeviceMapping_GetFluidicPhysAddr(uint8_t system_id);


/**
 * @brief Трансляция Системного ID сенсора/термостата в физический адрес
 */
DevicePhysAddr_t DeviceMapping_GetThermoPhysAddr(uint8_t system_id);



/**
 * @brief Возвращает битовую маску необходимых плат (NodeID) для выбранных модулей.
 * Бит 0 соответствует NodeID 0x20, бит 1 -> 0x21 и т.д.
 */
uint32_t DeviceMapping_GetRequiredNodesMask(uint8_t modules_mask);





#endif /* INC_DISPATCHER_DEVICE_MAPPING_H_ */
