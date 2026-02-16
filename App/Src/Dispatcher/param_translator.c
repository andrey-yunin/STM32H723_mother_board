/*
 * param_translator.c
 *
 *  Created on: Feb 6, 2026
 *      Author: andrey
 */

#include "param_translator.h"
// #include "app_config.h" // Включить, если константы определены в другом месте
// #include "logger.h"     // Включить для логирования ошибок/предупреждений

// --- Реализации функций ---

int32_t ParamTranslator_CuvetteToSteps(uint16_t cuvette_number) {
     if (cuvette_number == 0) {
         // Зарегистрировать предупреждение или вернуть конкретный код ошибки для недопустимого cuvette_number, если необходимо.
         // Logger_Warning("ParamTranslator: Invalid cuvette_number 0 for steps calculation.");
         return 0; // Предполагается, что 0 шагов означает "домой" или недопустимое значение.
     }
     // Простая линейная конверсия. Сложные сценарии могут включать таблицы поиска.
     return (int32_t)cuvette_number * PT_REACTION_DISK_STEPS_PER_CUVETTE_UNIT;
}

/** Пока закоментировали данную функцию до момента полной реализации процесса ## 3. Промывка системы
 *
 *
 uint32_t ParamTranslator_VolumeToPumpDurationMs(uint16_t volume_ul, uint8_t pump_id) {
     if (volume_ul == 0) {
         return 0; // Нет объема, нет продолжительности
         }

     if (volume_ul > PT_PUMP_MAX_VOLUME_UL) {
    	 // Logger_Warning("ParamTranslator: Volume %u exceeds max pump volume.", volume_ul);
    	 return 0; // Or return max duration
     }
     // В реальной системе pump_id будет использоваться для выбора правильной скорости потока.
     // Пока предполагается одна скорость потока или значение по умолчанию.
     // PT_DISPENSER_PUMP_FLOW_RATE_UL_PER_MS может быть массивом, индексируемым по pump_id.
     // Пример: volume / (flow_rate_ul_per_ms)
     // Избежать деления на ноль3
     if (PT_PUMP_DEFAULT_FLOW_RATE_UL_PER_MS == 0) {
    	 // Logger_Error("ParamTranslator: Pump flow rate is zero, cannot calculate duration.");
    	 return PT_PUMP_DEFAULT_FLOW_RATE_UL_PER_MS; // Вернуть безопасное значение по умолчанию
    	 }
     return (uint32_t)volume_ul / PT_PUMP_DEFAULT_FLOW_RATE_UL_PER_MS;
     }

 */

 /**
  *
  * @brief Преобразует номер слота на диске образцов в шаги мотора.  added 05/02/2026
  */
  int32_t ParamTranslator_SampleDiskSlotToSteps(uint16_t slot_number)
  {
	  // Простая линейная зависимость: количество шагов = номер слота * шагов_на_слот.
	  // В будущем здесь может быть более сложная логика, учитывающая
	  // начальные смещения, калибровочные таблицы и т.д.
	  if (slot_number == 0) {
		  // Возможно, 0 - это команда для поиска домашней позиции (homing)
		  // или просто невалидное значение. Пока возвращаем 0.
		  return 0;
		  }
	  return (int32_t)slot_number * SAMPLE_DISK_STEPS_PER_SLOT;
	  }


  /**
   * @brief Преобразует тип источника и позицию в шаги вращения дозатора. added 11/02/2026
   */
  int32_t ParamTranslator_DispenserSlotToRotateSteps(uint8_t source_type, uint16_t position)
  {
	  // TODO: Реализовать логику трансляции в зависимости от source_type и position.
	  // Пока - простая заглушка.
	  if (position == 0) return 0;
	  return (int32_t)position * DISPENSER_ROT_STEPS_PER_SLOT;
	  }

  /**
   * @brief Преобразует тип источника и позицию в шаги опускания иглы дозатора. added 11/02/2026
   */
  int32_t ParamTranslator_DispenserZToStepsDown(uint8_t source_type, uint16_t position)
  {
	  // TODO: Реализовать логику трансляции в зависимости от source_type и position.
	  // Пока - простая заглушка.
	  return DISPENSER_Z_STEPS_DOWN; // Возвращаем положительное значение
	  }

  /**
   * @brief Преобразует тип источника и позицию в шаги подъема иглы дозатора. added 11/02/2026
   */
  int32_t ParamTranslator_DispenserZToStepsUp(uint8_t source_type, uint16_t position)
  {
	  // TODO: Реализовать логику трансляции в зависимости от source_type и position.
	  // Пока - простая заглушка.
	  return DISPENSER_Z_STEPS_UP; // Возвращаем отрицательное значение для движения вверх
	  }

  /**
   * @brief Преобразует объем жидкости в длительность работы насоса в миллисекундах. added 11/02/2026
   */
  uint32_t ParamTranslator_VolumeToPumpDurationMs(uint8_t dispenser_id, uint16_t volume)
  {
	  uint8_t actual_pump_id;
	  // Реализовать маппинг dispenser_id -> actual_pump_id
	  // TODO: Здесь должна быть более сложная логика маппинга,
	  // возможно, из конфига или таблицы.
	  if (dispenser_id == 1) {
		  actual_pump_id = 1; // Предполагаем, что дозатор 1 использует насос 1
		  }
	  else {
		  // Для других dispenser_id по умолчанию используем pump_id = 1 или вернем ошибку

		  actual_pump_id = 1; // По умолчанию, чтобы избежать 0 и использовать в расчете
		  // TODO: Логировать ошибку или вернуть ERR_INVALID_PARAM, если dispenser_id не маппирован.
		  }

	  // Расчет длительности на основе объема и калибровки для конкретного насоса
	  // TODO: Implement pump-specific calibration constants (e.g., an array PUMP_CALIBRATION_MS_PER_UL[pump_id])

	  uint32_t ms_per_ul;
	  // Пример использования actual_pump_id для выбора константы
	  if (actual_pump_id == 1) { // Калибровка для Pump 1
		  ms_per_ul = PUMP_MS_PER_UL; // Используем общую константу пока нет специфичных
		  }
	  else {
		  // Для других насосов пока используем общую константу
		  ms_per_ul = PUMP_MS_PER_UL;
		  }
	  if (volume == 0)
		  return 0;

	  return (uint32_t)volume * ms_per_ul;
	  }




 /**
  * @brief Преобразует тип назначения и позицию в шаги вращения дозатора. added 13/02/2026
  */

  int32_t ParamTranslator_DispenserTargetToRotateSteps(uint8_t target_type, uint16_t position)
  {
	  // TODO: Реализовать логику трансляции в зависимости от target_type и position.
      // Пока - простая заглушка.
      if (position == 0) return 0;
      return (int32_t)position * DISPENSER_ROT_STEPS_PER_SLOT;
      }

 /**
  * @brief Преобразует тип назначения и позицию в шаги опускания иглы дозатора. added 13/02/2026
  */
  int32_t ParamTranslator_DispenserTargetZToStepsDown(uint8_t target_type, uint16_t position)
  {
	  // TODO: Реализовать логику трансляции в зависимости от target_type и position.
	  // Пока - простая заглушка.
	  return DISPENSER_Z_STEPS_DOWN; // Возвращаем положительное значение
	  }

 /**
  * @brief Преобразует тип назначения и позицию в шаги подъема иглы дозатора. added 13/02/2026
  */

 int32_t ParamTranslator_DispenserTargetZToStepsUp(uint8_t target_type, uint16_t position)
 {
	 // TODO: Реализовать логику трансляции в зависимости от target_type и position.
	 // Пока - простая заглушка.
	 return DISPENSER_Z_STEPS_UP; // Возвращаем отрицательное значение для движения вверх
	 }

 /**
  * @brief Преобразует ID ротора реагентов и номер слота в шаги мотора для поворота. added 13/02/2026
  */
 int32_t ParamTranslator_ReagentRotorSlotToSteps(uint8_t rotor_id, uint16_t slot_number)
 {
	 // TODO: Реализовать логику трансляции в зависимости от rotor_id и slot_number.
	 // Учитывать, что роторов может быть несколько, и у каждого своя механика/калибровка.
	 // Пока - простая заглушка.
	 if (slot_number == 0) return 0;
	 // Предполагаем, что для ротора реагентов есть константа STEPS_PER_SLOT
	 // Для примера используем 100 шагов на слот, но это нужно будет откалибровать.
	 return (int32_t)slot_number * ROTOR_REAGENT_STEPS_PER_SLOT; // ROTOR_REAGENT_STEPS_PER_SLOT (нужно будет определить)
	 }


 /**
  * @brief Преобразует номер кюветы в шаги мотора для поворота миксера.
  */
 int32_t ParamTranslator_MixerCuvetteToRotationSteps(uint16_t cuvette_number)
 {
	 // TODO: Реализовать логику трансляции в зависимости от cuvette_number.
     // Пока - простая заглушка.
     if (cuvette_number == 0) return 0;
     return (int32_t)cuvette_number * MIXER_ROT_STEPS_PER_CUVETTE;
     }

  /**
   * @brief Преобразует параметры миксера в шаги для опускания лопатки.
   */
 int32_t ParamTranslator_MixerZToStepsDown(uint8_t mixer_id, uint16_t cuvette_number)
 {
	 // TODO: Реализовать логику трансляции в зависимости от mixer_id и cuvette_number.
	 // Пока - простая заглушка.
	 return MIXER_Z_STEPS_DOWN; // Возвращаем положительное значение
	 }

  /**
   * @brief Преобразует параметры миксера в шаги для подъема лопатки.
   */
  int32_t ParamTranslator_MixerZToStepsUp(uint8_t mixer_id, uint16_t cuvette_number)
  {
	  // TODO: Реализовать логику трансляции в зависимости от mixer_id и cuvette_number.
	  // Пока - простая заглушка.
	  return MIXER_Z_STEPS_UP; // Возвращаем отрицательное значение для движения вверх
	  }


 // Добавить реализации других функций преобразования здесь



