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

// Константа, определяющая количество шагов мотора на один слот диска.
// ВАЖНО: Это значение - заглушка! Его необходимо будет откалибровать
// в соответствии с реальной механикой и драйвером мотора.
#define SAMPLE_DISK_STEPS_PER_SLOT 100


// Константы для трансляции параметров дозатора (заглушки)
#define DISPENSER_ROT_STEPS_PER_SLOT 200    // Шагов на слот для поворота дозатора
#define DISPENSER_Z_STEPS_DOWN       300    // Шагов для опускания иглы дозатора
#define DISPENSER_Z_STEPS_UP         -300    // Шагов для подъема иглы дозатора (величина)
#define PUMP_MS_PER_UL               10     // мс работы насоса на 1 микролитр


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
//uint32_t ParamTranslator_VolumeToPumpDurationMs(uint16_t volume_ul, uint8_t pump_id);


/**
 * @brief Преобразует номер слота на диске образцов в шаги мотора.
 *
 * @param slot_number Номер слота (позиции) на диске образцов.
 * @return int32_t Количество шагов для поворота мотора.
 */
 int32_t ParamTranslator_SampleDiskSlotToSteps(uint16_t slot_number);


 /**
  * @brief Преобразует тип источника и позицию в шаги вращения дозатора.
  *
  * @param source_type Тип источника (например, диск образцов, реагентов).
  * @param position Номер позиции/слота.
  * @return int32_t Количество шагов для поворота дозатора вокруг своей оси.
  */
  int32_t ParamTranslator_DispenserSlotToRotateSteps(uint8_t source_type, uint16_t position);


 /**
  * @brief Преобразует тип источника и позицию в шаги опускания иглы дозатора.
  *
  * @param source_type Тип источника.
  * @param position Номер позиции/слота.
  * @return int32_t Количество шагов для опускания иглы (положительное значение).
  */
  int32_t ParamTranslator_DispenserZToStepsDown(uint8_t source_type, uint16_t position);


 /**
  * @brief Преобразует тип источника и позицию в шаги подъема иглы дозатора.
  *
  * @param source_type Тип источника.
  * @param position Номер позиции/слота.
  * @return int32_t Количество шагов для подъема иглы (положительное значение).
  */
  int32_t ParamTranslator_DispenserZToStepsUp(uint8_t source_type, uint16_t position);


  /**
   * @brief Преобразует объем жидкости в длительность работы насоса в миллисекундах.
   *
   * @param dispenser_id ID дозатора (может влиять на калибровку насоса).
   * @param volume Объем в микролитрах.
   * @return uint32_t Длительность работы насоса в мс.
   */
  uint32_t ParamTranslator_VolumeToPumpDurationMs(uint8_t dispenser_id, uint16_t volume);


 /**
  * @brief Преобразует тип назначения и позицию в шаги вращения дозатора.
  *
  * @param target_type Тип назначения (например, реакционный диск, станция промывки).
  * @param position Номер позиции/слота.
  * @return int32_t Количество шагов для поворота дозатора вокруг своей оси.
  */
  int32_t ParamTranslator_DispenserTargetToRotateSteps(uint8_t target_type, uint16_t position);


 /**
  * @brief Преобразует тип назначения и позицию в шаги опускания иглы дозатора.
  *
  * @param target_type Тип назначения.
  * @param position Номер позиции/слота.
  * @return int32_t Количество шагов для опускания иглы (положительное значение).
  */
  int32_t ParamTranslator_DispenserTargetZToStepsDown(uint8_t target_type, uint16_t position);


 /**
  * @brief Преобразует тип назначения и позицию в шаги подъема иглы дозатора.
  *
  * @param target_type Тип назначения.
  * @param position Номер позиции/слота.
  * @return int32_t Количество шагов для подъема иглы (отрицательное значение).
  */
  int32_t ParamTranslator_DispenserTargetZToStepsUp(uint8_t target_type, uint16_t position);




// --- Add more translation functions as identified by the command analysis ---
// e.g., TemperatureToRawADC(), MicrometersToMotorSteps(), etc.




#endif /* INC_DISPATCHER_PARAM_TRANSLATOR_H_ */
