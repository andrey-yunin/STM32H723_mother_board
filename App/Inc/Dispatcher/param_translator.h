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

// Константы для трансляции параметров ротора реагентов (заглушки)
#define ROTOR_REAGENT_STEPS_PER_SLOT 100    // Шагов на слот для поворота ротора реагентов

// Константы для трансляции параметров миксера (заглушки)
//#define MIXER_ROT_MOTOR_ID           8     // ID мотора для поворота миксера по X-Y
//#define MIXER_Z_MOTOR_ID             9     // ID мотора для подъема/опускания лопатки миксера по Z
//#define MIXER_MIXING_MOTOR_ID        10    // ID мотора для перемешивания (вкл/выкл)

#define MIXER_Z_STEPS_UP             -200   // Шагов для опускания лопатки миксера
#define MIXER_Z_STEPS_DOWN           200  // Шагов для подъема лопатки миксера
#define MIXER_ROT_STEPS_PER_CUVETTE  100   // Шагов на кювету для поворота миксера




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
 * @return int32_t Количество шагов для поворота мотора. */
 int32_t ParamTranslator_SampleDiskSlotToSteps(uint16_t slot_number);


 /**
  * @brief Преобразует тип источника и позицию в шаги вращения дозатора.
  *
  * @param source_type Тип источника (например, диск образцов, реагентов).
  * @param position Номер позиции/слота.
  * @return int32_t Количество шагов для поворота дозатора вокруг своей оси.  */
  int32_t ParamTranslator_DispenserSlotToRotateSteps(uint8_t source_type, uint16_t position);


 /**
  * @brief Преобразует тип источника и позицию в шаги опускания иглы дозатора.
  *
  * @param source_type Тип источника.
  * @param position Номер позиции/слота.
  * @return int32_t Количество шагов для опускания иглы (положительное значение).  */
  int32_t ParamTranslator_DispenserZToStepsDown(uint8_t source_type, uint16_t position);


 /**
  * @brief Преобразует тип источника и позицию в шаги подъема иглы дозатора.
  *
  * @param source_type Тип источника.
  * @param position Номер позиции/слота.
  * @return int32_t Количество шагов для подъема иглы (положительное значение).  */
  int32_t ParamTranslator_DispenserZToStepsUp(uint8_t source_type, uint16_t position);


  /**
   * @brief Преобразует объем жидкости в длительность работы насоса в миллисекундах.
   *
   * @param dispenser_id ID дозатора (может влиять на калибровку насоса).
   * @param volume Объем в микролитрах.
   * @return uint32_t Длительность работы насоса в мс.   */
  uint32_t ParamTranslator_VolumeToPumpDurationMs(uint8_t dispenser_id, uint16_t volume);


 /**
  * @brief Преобразует тип назначения и позицию в шаги вращения дозатора.  *
  * @param target_type Тип назначения (например, реакционный диск, станция промывки).
  * @param position Номер позиции/слота.
  * @return int32_t Количество шагов для поворота дозатора вокруг своей оси.  */
  int32_t ParamTranslator_DispenserTargetToRotateSteps(uint8_t target_type, uint16_t position);


 /**
  * @brief Преобразует тип назначен// Константы для трансляции параметров ротора реагентов (заглушки)
   8 #define ROTOR_REAGENT_STEPS_PER_SLOT 100    // Шагов на слот для поворота ротора реагентовия и позицию в шаги опускания иглы дозатора.  *
  * @param target_type Тип назначения.
  * @param position Номер позиции/слота.
  * @return int32_t Количество шагов для опускания иглы (положительное значение).  */
  int32_t ParamTranslator_DispenserTargetZToStepsDown(uint8_t target_type, uint16_t position);


 /**
  * @brief Преобразует тип назначения и позицию в шаги подъема иглы дозатора.  *
  * @param target_type Тип назначения.
  * @param position Номер позиции/слота.
  * @return int32_t Количество шагов для подъема иглы (отрицательное значение).  */
  int32_t ParamTranslator_DispenserTargetZToStepsUp(uint8_t target_type, uint16_t position);



  /**
   * @brief Преобразует ID ротора реагентов и номер слота в шаги мотора для поворота.   *
   * @param rotor_id ID ротора реагентов.
   * @param slot_number Номер слота (позиции).
   * @return int32_t Количество шагов для поворота ротора.   */
  int32_t ParamTranslator_ReagentRotorSlotToSteps(uint8_t rotor_id, uint16_t slot_number);


  /**
   * @brief Преобразует номер кюветы в шаги мотора для поворота миксера.   *
   * @param cuvette_number Номер целевой кюветы.
   * @return int32_t Количество шагов для поворота миксера к кювете.   */
  int32_t ParamTranslator_MixerCuvetteToRotationSteps(uint16_t cuvette_number);

   /**
    * @brief Преобразует параметры миксера в шаги для опускания лопатки.    *
    * @param mixer_id ID миксера.
    * @param cuvette_number Номер целевой кюветы (для контекста, если нужен).
    * @return int32_t Количество шагов для опускания лопатки.
    */
  int32_t ParamTranslator_MixerZToStepsDown(uint8_t mixer_id, uint16_t cuvette_number);

   /**
    * @brief Преобразует параметры миксера в шаги для подъема лопатки.
    *
    * @param mixer_id ID миксера.
    * @param cuvette_number Номер целевой кюветы (для контекста, если нужен).
    * @return int32_t Количество шагов для подъема лопатки.
    */
   int32_t ParamTranslator_MixerZToStepsUp(uint8_t mixer_id, uint16_t cuvette_number);




// --- Add more translation functions as identified by the command analysis ---
// e.g., TemperatureToRawADC(), MicrometersToMotorSteps(), etc.




#endif /* INC_DISPATCHER_PARAM_TRANSLATOR_H_ */
