/*
 * recipe_store.h
 *
 *  Created on: Dec 3, 2025
 *      Author: andrey
 */

#ifndef SRC_DISPATCHER_INC_RECIPE_STORE_H_
#define SRC_DISPATCHER_INC_RECIPE_STORE_H_

#include <stdint.h>
#include "recipe_types.h"

// --- НОВОЕ ПЕРЕЧИСЛЕНИЕ --- 03.02.2026
/**
 * @brief Перечисление, определяющее источник значения для параметра атомарного действия.
 *        Позволяет JobManager'у универсально определять, брать ли параметр из рецепта
 *        или из команды пользователя.
 */
 typedef enum {
	 PARAM_SOURCE_STATIC,               // Значение берется из самого рецепта (по умолчанию)
	 PARAM_SOURCE_CMD_INIT_MASK,        // Значение используется для фильтрации по маске из cmd.args.init.modules_mask


	 // --- Смысловые параметры дозатора ---
	 PARAM_SOURCE_DISPENSER_ID,
	 PARAM_SOURCE_DISPENSER_ROTATE_STEPS,
	 PARAM_SOURCE_DISPENSER_Z_STEPS_DOWN,
	 PARAM_SOURCE_DISPENSER_Z_STEPS_UP,
	 PARAM_SOURCE_DISPENSER_SYRINGE_STEPS,
	 PARAM_SOURCE_DISPENSER_CYCLES,

	 // --- Смысловые параметры моющей станции ---
	 PARAM_SOURCE_WASH_STATION_CYCLES,
	 PARAM_SOURCE_WASH_STATION_FILL_DURATION_MS,

	 // --- Смысловые параметры реакционного диска ---
	 PARAM_SOURCE_REACTION_DISK_ROTATE_STEPS,

	 // --- Смысловые параметры диска образцов/реагентов ---
	 PARAM_SOURCE_REAGENT_SAMPLE_ROTATE_STEPS,

	 // --- Смысловые параметры миксера ---
	 PARAM_SOURCE_MIXER_ID,
	 PARAM_SOURCE_MIXER_XY_STEPS,
	 PARAM_SOURCE_MIXER_Z_STEPS_DOWN,
	 PARAM_SOURCE_MIXER_Z_STEPS_UP,
	 PARAM_SOURCE_MIXER_PADDLE_DURATION_MS,
	 PARAM_SOURCE_MIXER_WASH_CYCLES,


	 // --- Смысловые параметры фотометра ---
	 PARAM_SOURCE_PHOTOMETER_WAVELENGTH_MASK,


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
	 ACTION_RUN_PUMP_DURATION, // Recipe dosing: finite Fluidics command
     ACTION_START_PUMP,      // Включить насос Service/manual only
     ACTION_STOP_PUMP,       // Выключить насос // Service/manual/emergency only
     ACTION_WAIT_MS,         // Подождать N миллисекунд
     ACTION_HOME_MOTOR,      // Искать "домашнюю" позицию для мотора
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

          // <-- НОВАЯ СТРУКТУРА ДЛЯ ФОТОМЕТРА --> added 16/02/2026
          // Параметры для ACTION_PERFORM_SCAN
          struct {
        	  uint8_t photometer_id;         // ID фотометра (статический, для явности)
        	  ParamSource_t photometer_id_source;
        	  uint8_t wavelength_mask;       // Маска длин волн
        	  ParamSource_t wavelength_mask_source;
        	  } perform_scan;

           struct {
        		uint8_t pump_id;
        	    ParamSource_t pump_id_source;
        	    uint32_t duration_ms;
        	    ParamSource_t duration_ms_source;
        	    } pump_duration;

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
