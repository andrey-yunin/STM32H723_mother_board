/*
 * device_mapping.c
 *
 * РЕАЛИЗАЦИЯ ТРАНСЛЯЦИИ: СИСТЕМА -> УСТРОЙСТВО
 * -------------------------------------------
 * Этот модуль связывает логические роли анализатора с 
 * физическим подключением к платам-исполнителям.
 */

#include "Dispatcher/device_mapping.h"
#include "Dispatcher/system_mapping.h"
#include "Dispatcher/can_packer.h"

/**
 * @brief Маппинг МОТОРОВ на плату Motion (Node 0x20)
 */
DevicePhysAddr_t DeviceMapping_GetMotorPhysAddr(uint8_t system_id)
{
    DevicePhysAddr_t addr = {CAN_ADDR_MOTOR_BOARD, 0, true};

    switch (system_id) {
        case SYS_DISPENSER_MOTOR_XY:      addr.ch_idx = 0; break;
        case SYS_DISPENSER_MOTOR_Z:       addr.ch_idx = 1; break;
        case SYS_DISPENSER_MOTOR_SYRINGE: addr.ch_idx = 2; break;

        case SYS_REACTION_DISK_MOTOR:       addr.ch_idx = 3; break;
        case SYS_REAGENT_SAMPLE_DISK_MOTOR: addr.ch_idx = 4; break;

        case SYS_MIXER_MOTOR_XY:     addr.ch_idx = 5; break;
        case SYS_MIXER_MOTOR_Z:      addr.ch_idx = 6; break;
        case SYS_MIXER_PADDLE_MOTOR: addr.ch_idx = 7; break;

        default:
            addr.is_valid = false;
            break;
    }
    return addr;
}

/**
 * @brief Маппинг НАСОСОВ на плату Fluidic (Node 0x30)
 */
DevicePhysAddr_t DeviceMapping_GetFluidicPhysAddr(uint8_t system_id)
{
    DevicePhysAddr_t addr = {CAN_ADDR_PUMP_BOARD, 0, true};

    switch (system_id) {
        case SYS_WASH_PUMP_FILL:  addr.ch_idx = 10; break;
        case SYS_WASH_PUMP_DRAIN: addr.ch_idx = 11; break;
        
        default:
            addr.is_valid = false;
            break;
    }
    return addr;
}
