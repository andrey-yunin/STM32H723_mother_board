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
   * @brief Преобразует объём жидкости в шаги шприцевого мотора.
   *        Возвращает положительное значение. Знак определяется вызывающим кодом.
   */
   int32_t ParamTranslator_VolumeToSyringeSteps(uint16_t volume)
   {
	   if (volume == 0)
		   return 0;
	   return (int32_t)volume * SYRINGE_STEPS_PER_UL;
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

  /**
   * @brief Преобразует номер кюветы в шаги мотора для позиционирования фотометра.
   */
  int32_t ParamTranslator_PhotometerCuvetteToSteps(uint16_t cuvette_number)
  {
	  // TODO: Реализовать более сложную логику, учитывающую калибровку,
	  // начальное положение и т.д. Пока - простая заглушка.
	  if (cuvette_number == 0) return 0; // Или вернуть ошибку
	  return (int32_t)cuvette_number * PHOTOMETER_STEPS_PER_CUVETTE;
	  }


 /**
  * @brief Реализация ParamTranslator_DispenserWashRotateSteps.
  */
 int32_t ParamTranslator_DispenserWashRotateSteps(void)
 {
	 return DISPENSER_ROT_STEPS_TO_WASH_STATION;
	 }


 /**
  * @brief Реализация ParamTranslator_DispenserWashZToStepsDown.
  */
 int32_t ParamTranslator_DispenserWashZToStepsDown(void)
 {
	 return DISPENSER_Z_STEPS_DOWN_WASH_STATION;
	 }


 /**
  * @brief Реализация ParamTranslator_DispenserWashZToStepsUp.
  */
 int32_t ParamTranslator_DispenserWashZToStepsUp(void)
 {
	 return DISPENSER_Z_STEPS_UP_WASH_STATION;
	 }


 /**
  * @brief Преобразует объем жидкости в длительность работы насоса в миллисекундах
  *        для насоса заполнения моющей станции.
  * @param volume Объем в микролитрах.
  * @return uint32_t Длительность работы насоса в мс.
  */
 uint32_t ParamTranslator_WashStationFillVolumeToPumpDurationMs(uint16_t volume) {
	 if (volume == 0) {
		 return 0;
		 }

	 // Используем общую константу для преобразования объема в длительность
	 // В реальной системе может потребоваться более сложная логика
	 // с учетом калибровки конкретного насоса моющей станции.
	 return (uint32_t)volume * WASH_PUMP_MS_PER_UL;;
	 }





 // Добавить реализации других функций преобразования здесь



