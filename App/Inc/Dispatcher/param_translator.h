/*
 * param_translator.h
 *
 *  Created on: Feb 6, 2026
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_PARAM_TRANSLATOR_H_
#define INC_DISPATCHER_PARAM_TRANSLATOR_H_

#include <stdint.h>
#include <stdbool.h> // Для типа bool

// --- Константы конфигурации (примерные заполнители для реакционного диска) ---
// Эти значения аппаратно-специфичны и должны быть уточнены на основе фактических спецификаций устройства.
// В конечном итоге они могут быть перемещены в выделенный файл device_config.h или загружены динамически.
#define PT_REACTION_DISK_STEPS_PER_CUVETTE_UNIT 100 // Пример: 100 шагов для перемещения от одной кюветы к следующей
#define PT_REACTION_DISK_MAX_CUVETTE            40  // Пример: Максимум 40 кювет на реакционном диске
#define PT_REACTION_DISK_HOME_OFFSET_STEPS      50  // Example: Offset from physical home to cuvette 1 position


// --- Configuration Constants (example placeholders for pumps) ---
#define PT_PUMP_DEFAULT_FLOW_RATE_UL_PER_MS     10  // Example: 10 microliters per millisecond
#define PT_PUMP_MAX_VOLUME_UL                   5000 // Example: Max volume a pump might handle

// --- Translation Function Declarations ---

/**
 * @brief Translates a high-level cuvette number to low-level motor steps for the reaction disk.
 *        Assumes cuvette numbers are 1-based.
 *        Calculates absolute steps from a theoretical home position.
 * @param cuvette_number The 1-based number of the target cuvette (e.g., 1-40).
 * @return int32_t The total number of steps to reach that cuvette position.
                   Returns 0 if cuvette_number is 0 or invalid (e.g., > PT_REACTION_DISK_MAX_CUVETTE).
 */
int32_t ParamTranslator_CuvetteToSteps(uint16_t cuvette_number);

/**
 * @brief Translates a volume in microliters to a pump activation duration in milliseconds.
 *        This function would typically select a flow rate based on the pump_id.
 * @param volume_ul The volume to dispense/aspirate in microliters.
 * @param pump_id The ID of the pump (allows for pump-specific flow rates).
 * @return uint32_t The duration in milliseconds for the pump to operate.
 *                  Returns 0 if volume_ul is 0 or calculation is impossible (e.g., zero flow rate).
 */
uint32_t ParamTranslator_VolumeToPumpDurationMs(uint16_t volume_ul, uint8_t pump_id);


// --- Add more translation functions as identified by the command analysis ---
// e.g., TemperatureToRawADC(), MicrometersToMotorSteps(), etc.




#endif /* INC_DISPATCHER_PARAM_TRANSLATOR_H_ */
