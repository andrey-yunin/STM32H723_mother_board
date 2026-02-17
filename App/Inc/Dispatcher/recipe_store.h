/*
 * recipe_store.h
 *
 *  Created on: Dec 3, 2025
 *      Author: andrey
 */

#ifndef SRC_DISPATCHER_INC_RECIPE_STORE_H_
#define SRC_DISPATCHER_INC_RECIPE_STORE_H_

#include <stdint.h>
#include "command_parser.h" // Для доступа к RecipeID_t

// --- НОВОЕ ПЕРЕЧИСЛЕНИЕ --- 03.02.2026
/**
 * @brief Перечисление, определяющее источник значения для параметра атомарного действия.
 *        Позволяет JobManager'у универсально определять, брать ли параметр из рецепта
 *        или из команды пользователя.
 */
 typedef enum {
	 PARAM_SOURCE_STATIC,               // Значение берется из самого рецепта (по умолчанию)
	 PARAM_SOURCE_CMD_INIT_MASK,        // Значение используется для фильтрации по маске из cmd.args.init.modules_mask


	 // --- параметры для DISPENSER_WASH --- modified 16/02/2026
	 PARAM_SOURCE_CMD_DISPENSER_ID,     // Значение берется из cmd.args.dispenser_wash.dispenser_id
	 PARAM_SOURCE_CMD_DISPENSER_WASH_PUMP_DURATION_MS, // НОВОЕ: Длительность работы насоса для DISPENSER_WASH
	 PARAM_SOURCE_CMD_DISPENSER_CYCLES, // Значение берется из cmd.args.dispenser_wash.cycles
	 PARAM_SOURCE_CMD_DISPENSER_WASH_STEPS_DOWN, // Шаги для опускания иглы в процессе DISPENSER_WASH
	 PARAM_SOURCE_CMD_DISPENSER_WASH_STEPS_UP,   // Шаги для подъема иглы в процессе DISPENSER_WASH
	 PARAM_SOURCE_CMD_DISPENSER_WASH_ROTATE_STEPS, // Шаги для вращения дозатора в процессе DISPENSER_WASH

	 // --- параметры для WASH_STATION_WASH --- added 05/02/2026
	 PARAM_SOURCE_CMD_WASH_STATION_WASH_CYCLES, // Это относится к циклам из команды, которые будут обрабатываться JobManager_Run для повторения рецепта
	 PARAM_SOURCE_CMD_WASH_STATION_WASH_ROTATE_STEPS, // Для рассчитанных шагов поворота диска


	 // ---конец параметров для WASH_STATION_WASH ---

	 // --- Параметры для SAMPLE_ROTATE --- added 11/02/2026
	 PARAM_SOURCE_CMD_SAMPLE_ROTATE_STEPS, // источник для рассчитанных шагов поворота диска образцов

	 // --- Параметры для DISPENSER_ASPIRATE --- added 11/02/2026
	 PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_DISPENSER_ID,
	 PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_SOURCE_TYPE,
	 PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_ROTATE_STEPS,
	 PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_STEPS_DOWN,
	 PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_STEPS_UP,
	 PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_PUMP_DURATION_MS,

	 // --- Параметры для DISPENSER_DISPENSE --- added 13/02/2026
	 PARAM_SOURCE_CMD_DISPENSER_DISPENSE_DISPENSER_ID,
	 PARAM_SOURCE_CMD_DISPENSER_DISPENSE_TARGET_TYPE,
	 PARAM_SOURCE_CMD_DISPENSER_DISPENSE_ROTATE_STEPS,
	 PARAM_SOURCE_CMD_DISPENSER_DISPENSE_STEPS_DOWN,
	 PARAM_SOURCE_CMD_DISPENSER_DISPENSE_STEPS_UP,
	 PARAM_SOURCE_CMD_DISPENSER_DISPENSE_PUMP_DURATION_MS,

	 // --- Параметры для REAGENT_ROTATE --- --- added 13/02/2026
	 PARAM_SOURCE_CMD_REAGENT_ROTATE_ROTOR_ID,
	 PARAM_SOURCE_CMD_REAGENT_ROTATE_STEPS,

	 // --- Параметры для MIXER_MIX --- --- added 13/02/2026
	 PARAM_SOURCE_CMD_MIXER_MIX_MIXER_ID,
	 PARAM_SOURCE_CMD_MIXER_MIX_ROTATE_STEPS,
	 PARAM_SOURCE_CMD_MIXER_MIX_DURATION_MS,
	 PARAM_SOURCE_CMD_MIXER_MIX_WASH_CYCLES,
	 PARAM_SOURCE_CMD_MIXER_MIX_Z_STEPS_DOWN,
	 PARAM_SOURCE_CMD_MIXER_MIX_Z_STEPS_UP,


	 // --- Параметры для PHOTOMETER_SCAN_SINGLE ---
	 PARAM_SOURCE_CMD_PHOTOMETER_SCAN_SINGLE_ROTATE_STEPS,
	 PARAM_SOURCE_CMD_PHOTOMETER_SCAN_SINGLE_WAVELENGTH_MASK,

	 // --- Параметры для WASH_STATION_FILL --- added 17/02/2026
	 PARAM_SOURCE_CMD_WASH_STATION_FILL_PUMP_DURATION_MS,
	 PARAM_SOURCE_CMD_WASH_STATION_FILL_ROTATE_STEPS,



	 // Добавьте другие источники по мере необходимости

	 PARAM_SOURCE_MAX
	 } ParamSource_t;
	 // --- КОНЕЦ НОВОГО ПЕРЕЧИСЛЕНИЯ ---

/**
  * @brief "Ingredients base": types of atomic actions for all recipes.
  */
 typedef enum {
     ACTION_NONE = 0,        // Маркер конца рецепта, "Ничего не делать"
     ACTION_ROTATE_MOTOR,    // Вращать мотор на N шагов
     ACTION_START_PUMP,      // Включить насос
     ACTION_STOP_PUMP,       // Выключить насос
     ACTION_WAIT_MS,         // Подождать N миллисекунд
     ACTION_HOME_MOTOR,      // Искать "домашнюю" позицию для мотора
	 ACTION_START_MIXING_MOTOR, // <-- НОВОЕ ДЕЙСТВИЕ: Включить мотор перемешивания added 13/02/2026
	 ACTION_STOP_MIXING_MOTOR,  // <-- НОВОЕ ДЕЙСТВИЕ: Выключить мотор перемешивани added 13/02/2026
	 ACTION_PERFORM_SCAN, // <-- НОВОЕ ДЕЙСТВИЕ: Выполнить сканирование фотометром added 16/02/2026
     // ... Другие будущие действия ...
 } ActionType_t;


 /**
  * @brief Structure of "Atomic action" It describes one concrete action end its parameters.
  */
 typedef struct {
     // What to do:
     ActionType_t action;

     // Arguments and parameters for action
     union {
         // Параметры для ACTION_ROTATE_MOTOR
         struct {
             uint8_t motor_id;
             ParamSource_t motor_id_source; // NEW!
             int32_t steps; // Положительное/отрицательное число шагов
             ParamSource_t steps_source;    // NEW!
             uint16_t speed;
             ParamSource_t speed_source;    // NEW!
         } rotate_motor;

         // Параметры для ACTION_START_PUMP / ACTION_STOP_PUMP
         struct {
             uint8_t pump_id;
             ParamSource_t pump_id_source; // NEW!
         } pump;

         // Параметры для ACTION_WAIT_MS
         struct {
             uint32_t delay_ms;
             ParamSource_t delay_ms_source; // NEW!
         } wait;

         // Параметры для ACTION_HOME_MOTOR
         struct {
             uint8_t motor_id;
             ParamSource_t motor_id_source; // NEW!
             uint16_t speed; // Скорость поиска
             ParamSource_t speed_source;    // NEW!
         } home_motor;


         // <-- НОВАЯ СТРУКТУРА ДЛЯ МОТОРА ПЕРЕМЕШИВАНИЯ --> added 13/02/2026
         // Параметры для ACTION_START_MIXING_MOTOR / ACTION_STOP_MIXING_MOTOR
         struct {
        	 uint8_t mixer_id; // ID миксера (или мотора перемешивания)
        	 ParamSource_t mixer_id_source;

        	 // Возможно, тут нужны параметры для скорости/мощности, но для простоты пока только ID
          } mixing_motor;

          // <-- НОВАЯ СТРУКТУРА ДЛЯ ФОТОМЕТРА --> added 16/02/2026
          // Параметры для ACTION_PERFORM_SCAN
          struct {
        	  uint8_t photometer_id;         // ID фотометра (статический, для явности)
        	  ParamSource_t photometer_id_source;
        	  uint8_t wavelength_mask;       // Маска длин волн
        	  ParamSource_t wavelength_mask_source;
        	  } perform_scan;
     } params;
 } AtomicAction_t; //

  /**
  * @brief Structure "Recipe step".
  *        Представляет собой группу из одного или нескольких атомарных действий,
  *        которые должны быть запущены одновременно.
  */
 typedef struct {
     const AtomicAction_t* atomic_actions; // Указатель на массив атомарных действий
     uint8_t num_actions;                  // Количество действий в этой группе (в шаге)
 } ProcessStep_t; //


 /**
  * @brief API "Recipe store": return recipe according to its ID.
  *
  * @param id Идентификатор рецепта (из command_parser.h).
  * @return const ProcessStep_t* Указатель на первый шаг рецепта (во Flash).
  */
 const ProcessStep_t* Recipe_Get(RecipeID_t id);

#endif /* SRC_DISPATCHER_INC_RECIPE_STORE_H_ */
