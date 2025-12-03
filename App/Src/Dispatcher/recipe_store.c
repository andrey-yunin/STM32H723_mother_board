/*
 * recipe_store.c
 *
 *  Created on: Dec 3, 2025
 *      Author: andrey
 */

#include "recipe_store.h"
#include <stddef.h> // Для NULL

 // ============================================================================
 // ---                  ХРАНИЛИЩЕ РЕЦЕПТОВ (во Flash-памяти)                ---
 // ============================================================================

 /**
  * @brief Рецепт: Инициализация всей системы (Homing).
  *
  * Выполняется один раз при старте для приведения механизмов в известное положение.
  */
 const ProcessStep_t g_recipe_initialize_system[] = {
     // Шаг 1: Поиск "дома" для иглы (мотор 2). Группа из ОДНОГО действия.
     {
         .atomic_actions = (const AtomicAction_t[]){ // Составной литерал для группы из одного действия
             { .action = ACTION_HOME_MOTOR, .params.home_motor = { .motor_id=2, .speed=150 } }
         },
         .num_actions = 1
     },

     // Шаг 2: Поиск "дома" для дозатора (мотор 1). Группа из ОДНОГО действия.
     {
         .atomic_actions = (const AtomicAction_t[]){ // Составной литерал для группы из одного действия
             { .action = ACTION_HOME_MOTOR, .params.home_motor = { .motor_id=1, .speed=400 } }
         },
         .num_actions = 1
     },

     // Маркер конца рецепта. Группа из НУЛЯ действий.
     { .atomic_actions = NULL, .num_actions = 0 }
 };

 /**
  * @brief Рецепт: Взять реагент (Aspirate Reagent). Пример смешанного рецепта.
  */
 const ProcessStep_t g_recipe_aspirate_reagent[] = {
     // Шаг 1: Поворот дозатора (мотор 1) к пробирке. Группа из ОДНОГО действия.
     {
         .atomic_actions = (const AtomicAction_t[]){
             { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = { .motor_id=1, .steps=1000, .speed=500 } }
         },
         .num_actions = 1
     },

     // Шаг 2: Опускание иглы (мотор 2). Группа из ОДНОГО действия.
     {
         .atomic_actions = (const AtomicAction_t[]){
             { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = { .motor_id=2, .steps=200, .speed=100 } }
         },
         .num_actions = 1
     },

     // Шаг 3: Включение насоса и небольшая пауза. Группа из ДВУХ действий (параллельно).
     {
         .atomic_actions = (const AtomicAction_t[]){
             { .action = ACTION_START_PUMP,   .params.pump = { .pump_id=1 } },
             { .action = ACTION_WAIT_MS,      .params.wait = { .delay_ms=500 } }
         },
         .num_actions = 2
     },

     // Шаг 4: Выключение насоса. Группа из ОДНОГО действия.
     {
         .atomic_actions = (const AtomicAction_t[]){
             { .action = ACTION_STOP_PUMP,    .params.pump = { .pump_id=1 } }
         },
         .num_actions = 1
     },

     // Шаг 5: Поднятие иглы (мотор 2 в обратную сторону). Группа из ОДНОГО действия.
     {
         .atomic_actions = (const AtomicAction_t[]){
             { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = { .motor_id=2, .steps=-200, .speed=100 } }
         },
         .num_actions = 1
     },

     // Шаг 6: Возврат дозатора (мотор 1 в обратную сторону). Группа из ОДНОГО действия.
     {
         .atomic_actions = (const AtomicAction_t[]){
             { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = { .motor_id=1, .steps=-1000, .speed=500 } }
         },
         .num_actions = 1
     },

     // Маркер конца рецепта. Группа из НУЛЯ действий.
     { .atomic_actions = NULL, .num_actions = 0 }
 };


 // ============================================================================
 // ---                 API "Recipe store" (Оглавление)                   ---
 // ============================================================================

 /**
  * @brief Находит и возвращает указатель на запрошенный рецепт.
    98  */
 const ProcessStep_t* Recipe_Get(RecipeID_t id)
 {
     switch (id)
     {
        case RECIPE_INITIALIZE_SYSTEM:
             return g_recipe_initialize_system;

        case RECIPE_ASPIRATE:
             return g_recipe_aspirate_reagent;

        default:
             return NULL;
     }
 }
