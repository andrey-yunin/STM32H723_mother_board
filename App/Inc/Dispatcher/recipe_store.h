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
     PARAM_SOURCE_CMD_DISPENSER_ID,     // Значение берется из cmd.args.dispenser_wash.dispenser_id
     PARAM_SOURCE_CMD_DISPENSER_VOLUME, // Значение берется из cmd.args.dispenser_wash.volume
     PARAM_SOURCE_CMD_DISPENSER_CYCLES, // Значение берется из cmd.args.dispenser_wash.cycles

	 // --- параметры для WASH_STATION_WASH --- added 05/02/2026
	 PARAM_SOURCE_CMD_WASH_STATION_WASH_CYCLES, // Это относится к циклам из команды, которые будут обрабатываться JobManager_Run для повторения рецепта
	 PARAM_SOURCE_CMD_WASH_STATION_WASH_ROTATE_STEPS, // Для рассчитанных шагов поворота диска
	 // ---конец параметров для WASH_STATION_WASH ---

	 // --- Параметры для SAMPLE_ROTATE --- added 11/02/2026
	 PARAM_SOURCE_CMD_SAMPLE_ROTATE_STEPS, // источник для рассчитанных шагов поворота диска образцов

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
