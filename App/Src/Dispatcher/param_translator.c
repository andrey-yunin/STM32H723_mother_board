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

 /**
  * @brief Преобразует номер слота на диске образцов в шаги мотора.
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


 // Добавить реализации других функций преобразования здесь



