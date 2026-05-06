/*
 * system_mapping.h
 *
 * СЛОЙ СИСТЕМНОЙ ЛОГИКИ (Host <-> Conductor)
 * ------------------------------------------
 * Содержит идентификаторы логических узлов анализатора.
 * Используется в протоколе с Хостом и в Рецептах.
 */

#ifndef INC_DISPATCHER_SYSTEM_MAPPING_H_
#define INC_DISPATCHER_SYSTEM_MAPPING_H_

#include <stdint.h>

// --- Моторы (System IDs) ---
#define SYS_DISPENSER_MOTOR_XY       1
#define SYS_DISPENSER_MOTOR_Z        2
#define SYS_DISPENSER_MOTOR_SYRINGE  3

#define SYS_REACTION_DISK_MOTOR          10
#define SYS_REAGENT_SAMPLE_DISK_MOTOR    11

#define SYS_MIXER_MOTOR_XY           20
#define SYS_MIXER_MOTOR_Z            21

// --- Силовые нагрузки Fluidics/Power (System IDs) ---
#define SYS_WASH_PUMP_FILL           10
#define SYS_WASH_PUMP_DRAIN          11
#define SYS_MIXER_PADDLE_LOAD        12  // Лопатка миксера: силовая finite duration нагрузка.
//   ch 13..15 -> клапаны


// --- Фотометр (System IDs) ---
#define SYS_PHOTOMETER_MAIN          1

#endif /* INC_DISPATCHER_SYSTEM_MAPPING_H_ */
