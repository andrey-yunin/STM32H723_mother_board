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
             int32_t steps; // Положительное/отрицательное число шагов
             uint16_t speed;
         } rotate_motor;

         // Параметры для ACTION_START_PUMP / ACTION_STOP_PUMP
         struct {
             uint8_t pump_id;
         } pump;

         // Параметры для ACTION_WAIT_MS
         struct {
             uint32_t delay_ms;
         } wait;

         // Параметры для ACTION_HOME_MOTOR
         struct {
             uint8_t motor_id;
             uint16_t speed; // Скорость поиска
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
