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

 /**
  * @brief Шаблон рецепта: Промывка дозатора (DISPENSER_WASH).
  *
  * @note Это шаблон для ОДНОГО цикла промывки. JobManager будет использовать
  *       параметры из команды (dispenser_id, volume, cycles), чтобы адаптировать
  *       этот шаблон при выполнении.
  */
const ProcessStep_t g_recipe_dispenser_wash[] = {

		// Шаг 1: Поворот дозатора (Мотор 1) к промывочной станции.
		{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = { .motor_id = 1, .steps = 2000, .speed = 800 } }
					},
					.num_actions = 1
					},

		// Шаг 2: Опускание иглы (Мотор 2) в промывочную станцию.
		{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = { .motor_id = 2, .steps = 500, .speed = 400 } }
					},
					.num_actions = 1
					},

		// Шаг 3: Включение насоса и короткая пауза для заполнения.
		// JobManager будет адаптировать время паузы под параметр 'volume'.
		{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_START_PUMP, .params.pump = { .pump_id = 1 } },
					{ .action = ACTION_WAIT_MS,    .params.wait = { .delay_ms = 500 } }
					},
					.num_actions = 2 // Два действия выполняются одновременно
					},

		// Шаг 4: Выключение насоса.
		{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_STOP_PUMP, .params.pump = { .pump_id = 1 } }
					},
					.num_actions = 1
					},

		// Шаг 5: Поднятие иглы (Мотор 2).
		{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = { .motor_id = 2, .steps = -500, .speed = 400 } }
					},
					.num_actions = 1
					},

		// Шаг 6: Возврат дозатора (Мотор 1) в исходное положение.
		{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = { .motor_id = 1, .steps = -2000, .speed = 800 } }
					},
		.num_actions = 1
		},

		// Маркер конца рецепта.
		{ .atomic_actions = NULL, .num_actions = 0 }
		};

/**
 * @brief Рецепт: Промывка кюветы на моющей станции (WASH_STATION_WASH). added 05/02/20276
 *
 * @note Это шаблон для ОДНОГО цикла промывки. JobManager будет использовать
 *       параметры из команды (cycles, cuvette), чтобы адаптировать
 *       этот шаблон при выполнении.
 */

const ProcessStep_t g_recipe_wash_station_wash[] = {
		// Шаг 1: Поворот реакционного диска (Мотор 3) к моющей станции.
		// `steps` будут взяты из значения cuvette в команде.
		{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_ROTATE_MOTOR,
						.params.rotate_motor = {
							.motor_id = 3, .motor_id_source = PARAM_SOURCE_STATIC, // ID мотора для реакционного диска (предположение)
							.steps = 3000, .steps_source = PARAM_SOURCE_CMD_WASH_STATION_WASH_CUVETTE, // Шаги берутся из параметра cuvette
							.speed = 1000, .speed_source = PARAM_SOURCE_STATIC // Скорость фиксирована}
					}}
				},
					.num_actions = 1
					},

		// Шаг 2: Заполнение кюветы (Насос 2).
		// Длительность заполнения фиксирована.
    	{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_START_PUMP,
						.params.pump = { .pump_id = 2, .pump_id_source = PARAM_SOURCE_STATIC } }, // ID насоса заполнения (предположение)
					{ .action = ACTION_WAIT_MS,    .params.wait = { .delay_ms = 1000, .delay_ms_source = PARAM_SOURCE_STATIC }} // Задержка фиксирована
					},
					.num_actions = 2
					},

		// Шаг 3: Остановка насоса заполнения.
		{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_STOP_PUMP, .params.pump = { .pump_id = 2, .pump_id_source = PARAM_SOURCE_STATIC } } // ID насоса заполнения (предположение)
					},
					.num_actions = 1
					},

		// Шаг 4: Слив из кюветы (Насос 3).
		// TODO: delay_ms должен быть рассчитан динамически.
		{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_START_PUMP, .params.pump = { .pump_id = 3, .pump_id_source = PARAM_SOURCE_STATIC } },
					{ .action = ACTION_WAIT_MS, .params.wait = { .delay_ms = 1000, .delay_ms_source = PARAM_SOURCE_STATIC } }
					},
					.num_actions = 2
					},

		// Шаг 5: Остановка насоса слива.
		{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_STOP_PUMP, .params.pump = { .pump_id = 3, .pump_id_source = PARAM_SOURCE_STATIC } }
					},
					.num_actions = 1
					},

		// Маркер конца рецепта.
		{
				.atomic_actions = NULL, .num_actions = 0 }
		};



// --- [ADD_NEW_COMMAND] ---
// 3. Скопируйте существующий рецепт как шаблон и создайте здесь свой,
//    например, g_recipe_wash_cuvette.





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

        case RECIPE_DISPENSER_WASH:
        	return g_recipe_dispenser_wash;

        case RECIPE_WASH_STATION_WASH: // added 05/02/2026
        	return g_recipe_wash_station_wash;

        // --- [ADD_NEW_COMMAND] ---
        // 4. Добавьте `case` для вашего нового рецепта здесь
        // case RECIPE_WASH_CUVETTE:
        //     return g_recipe_wash_cuvette;

        default:
             return NULL;
     }
 }
