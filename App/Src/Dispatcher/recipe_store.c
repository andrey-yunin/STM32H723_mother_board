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
		 // Шаг 1: Поворот дозатора (Мотор 5 - X-Y ось дозатора) к промывочной станции.
		 {.atomic_actions = (const AtomicAction_t[]){
			 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					 .motor_id = 5,
					 .motor_id_source = PARAM_SOURCE_STATIC,
					 .steps = 0, // Placeholder, will be replaced by source
					 .steps_source = PARAM_SOURCE_CMD_DISPENSER_WASH_ROTATE_STEPS,
					 .speed = 800,
					 .speed_source = PARAM_SOURCE_STATIC
				}}
			 },
			 .num_actions = 1
			 },

		 // Шаг 2: Опускание иглы (Мотор 3 - Z-ось дозатора) в промывочную станцию.
			 {.atomic_actions = (const AtomicAction_t[]){
				 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					 .motor_id = 3,
					 .motor_id_source = PARAM_SOURCE_STATIC,
					 .steps = 0, // Placeholder, will be replaced by source
					 .steps_source = PARAM_SOURCE_CMD_DISPENSER_WASH_STEPS_DOWN,
					 .speed = 400,
					 .speed_source = PARAM_SOURCE_STATIC
				}}
			 },
			 .num_actions = 1
			 },


		  // Шаг 3: Включение насоса и короткая пауза для заполнения.
		  // JobManager будет адаптировать время паузы под параметр 'volume'.
			 {.atomic_actions = (const AtomicAction_t[]){
				 { .action = ACTION_START_PUMP, .params.pump = {
					 .pump_id = 1, // Placeholder, will be replaced by source
					 .pump_id_source = PARAM_SOURCE_STATIC
				}},
				{ .action = ACTION_WAIT_MS,    .params.wait = {
					 .delay_ms = 0, // Placeholder, will be replaced by source
					 .delay_ms_source = PARAM_SOURCE_CMD_DISPENSER_WASH_PUMP_DURATION_MS
				}}
			},
			.num_actions = 2 // Два действия выполняются одновременно
			},


			// Шаг 4: Выключение насоса.
			{.atomic_actions = (const AtomicAction_t[]){
				 { .action = ACTION_STOP_PUMP, .params.pump = {
					  .pump_id = 1, // Placeholder, will be replaced by source
					  .pump_id_source = PARAM_SOURCE_STATIC
				 }}
			 },
			 .num_actions = 1
			 },


			 // Шаг 5: Поднятие иглы (Мотор 3 - Z-ось дозатора).
			 {.atomic_actions = (const AtomicAction_t[]){
				 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					   .motor_id = 3,
					   .motor_id_source = PARAM_SOURCE_STATIC,
					   .steps = 0, // Placeholder, will be replaced by source
					   .steps_source = PARAM_SOURCE_CMD_DISPENSER_WASH_STEPS_UP,
					   .speed = 400,
					   .speed_source = PARAM_SOURCE_STATIC
				  }}
			  },
			  .num_actions = 1
			  },


			  // Шаг 6: Возврат дозатора (Мотор 5 - X-Y ось дозатора) в исходное положение.
			  {.atomic_actions = (const AtomicAction_t[]){
				  { .action = ACTION_HOME_MOTOR,
					    .params.home_motor = { // Use HOME_MOTOR for consistency with other home actions
						.motor_id = 5,
						.motor_id_source = PARAM_SOURCE_STATIC,
						.speed = 800, // Speed for homing
						.speed_source = PARAM_SOURCE_STATIC
				   }}
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
							.steps = 3000, .steps_source = PARAM_SOURCE_CMD_WASH_STATION_WASH_ROTATE_STEPS, // Шаги берутся из рассчитанного параметра
							.speed = 1000, .speed_source = PARAM_SOURCE_STATIC // Скорость фиксирована
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

// Определение шагов для рецепта SAMPLE_ROTATE (0x5110) added 11/02/2026

const ProcessStep_t g_recipe_sample_rotate[] = {
		{ .atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					.motor_id = 4,       // Идентификатор мотора диска образцов (статический)
					.motor_id_source = PARAM_SOURCE_STATIC, // Источник ID мотора - статический
					.steps = 0,                             // Статическое значение будет перезаписано динамически
					.steps_source = PARAM_SOURCE_CMD_SAMPLE_ROTATE_STEPS, // Шаги из команды SAMPLE_ROTATE
					.speed = 1000,                          // Скорость вращения (статическая)
					.speed_source = PARAM_SOURCE_STATIC     // Источник скорости - статическая
					} }
			}, .num_actions = 1 },
			// Маркер конца рецепта
			{ .atomic_actions = NULL, .num_actions = 0 }
		};


/**
 * @brief Рецепт: Забор образца/реагента дозатором. added 11/02/2026
 * Выполняется командой DISPENSER_ASPIRATE (0x2100).
 */

const ProcessStep_t g_recipe_dispenser_aspirate[] = {
		// Шаг 1: Поворот дозатора к источнику (если требуется)
		// Предполагается, что мотор ID 5 - это ось вращения дозатора (предположение).

		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					.motor_id = 5, // ID мотора для оси X дозатора (предположение)
					.motor_id_source = PARAM_SOURCE_STATIC,
					.steps = 0, // Статическое значение (заглушка), будет перезаписано динамическим параметром из cmd->args.dispenser_aspirate.rotate_steps
					.steps_source = PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_ROTATE_STEPS, // Источник для rotate_steps

					.speed = 500, // Статическая скорость
					.speed_source = PARAM_SOURCE_STATIC
					}}
				},
				.num_actions = 1 // Одно движение
		},


		// Шаг 2: Опускание иглы (Z-ось)
		// Предполагается, что мотор ID 3 - это ось Z дозатора.

		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					.motor_id = 3, // ID мотора для оси X дозатора (предположение)
					.motor_id_source = PARAM_SOURCE_STATIC,
					.steps = 0, // Статическое значение (заглушка), будет перезаписано динамическим параметром из cmd->args.dispenser_aspirate.steps_down
					.steps_source = PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_STEPS_DOWN, // Источник для steps_down,
					.speed = 200, // Статическая скорость
					.speed_source = PARAM_SOURCE_STATIC
					}}
				},
				.num_actions = 1 // Одно движение
		},

		// Шаг 3: Запуск насоса для забора жидкости и ожидание

		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_START_PUMP, .params.pump = {
					.pump_id = 0,  // Статическое значение (заглушка), будет перезаписано динамическим параметром
					.pump_id_source = PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_DISPENSER_ID
					}},

			{ .action = ACTION_WAIT_MS, .params.wait = {
					.delay_ms = 0, // Статическое значение (заглушка), будет перезаписано динамическим параметром
					.delay_ms_source = PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_PUMP_DURATION_MS // Источник для pump_duration_ms

					}}
				},
				.num_actions = 2 // Насос запускается и ожидание происходит одновременно
			},

		// Шаг 3: Остановка насоса

		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_STOP_PUMP, .params.pump = {
					.pump_id = 0, // Статическое значение (заглушка), будет перезаписано динамическим параметром
					.pump_id_source = PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_DISPENSER_ID
					}}
				},
				.num_actions = 1
			},

			 // Шаг 5: Подъем иглы (Z-ось)
			{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
						.motor_id = 3, // ID мотора для оси Z дозатора (предположение)
						.motor_id_source = PARAM_SOURCE_STATIC,
						.steps = 0, // Статическое значение (заглушка), будет перезаписано динамическим параметром
						.steps_source = PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_STEPS_UP, // Источник для steps_up
						.speed = 200, // Статическая скорость
						.speed_source = PARAM_SOURCE_STATIC
					}}
				},
				.num_actions = 1
			},

		// Маркер конца рецепта
			{ .atomic_actions = NULL, .num_actions = 0 }
		};

/**
 * @brief Рецепт: Выдача образца/реагента дозатором.  added 13/02/2026
 * Выполняется командой DISPENSER_DISPENSE (0x2200).
 */
 const ProcessStep_t g_recipe_dispenser_dispense[] = {
		 // Шаг 1: Поворот дозатора к цели
		 { .atomic_actions = (const AtomicAction_t[]){
			 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					 .motor_id = 5,
					 .motor_id_source = PARAM_SOURCE_STATIC,
					 .steps = 0,
					 .steps_source = PARAM_SOURCE_CMD_DISPENSER_DISPENSE_ROTATE_STEPS,
					 .speed = 500,
					 .speed_source = PARAM_SOURCE_STATIC
				}}
			 },
			 .num_actions = 1
		},

		// Шаг 2: Опускание иглы (Z-ось)
		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					.motor_id = 3,
					.motor_id_source = PARAM_SOURCE_STATIC,
					.steps = 0,
					.steps_source = PARAM_SOURCE_CMD_DISPENSER_DISPENSE_STEPS_DOWN,
					.speed = 200,
					.speed_source = PARAM_SOURCE_STATIC
				}}
			},
			.num_actions = 1
		},

		// Шаг 3: Запуск насоса для выдачи жидкости и ожидание
		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_START_PUMP, .params.pump = {
					.pump_id = 0,
					.pump_id_source = PARAM_SOURCE_CMD_DISPENSER_DISPENSE_DISPENSER_ID
				}},
			{ .action = ACTION_WAIT_MS, .params.wait = {
					.delay_ms = 0,
					.delay_ms_source = PARAM_SOURCE_CMD_DISPENSER_DISPENSE_PUMP_DURATION_MS
				}}
			},
			.num_actions = 2
		},

		// Шаг 4: Остановка насоса
		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_STOP_PUMP, .params.pump = {
					.pump_id = 0,
					.pump_id_source = PARAM_SOURCE_CMD_DISPENSER_DISPENSE_DISPENSER_ID
				}}
			},
		.num_actions = 1
		},

		// Шаг 5: Подъем иглы (Z-ось)
		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					.motor_id = 3,
					.motor_id_source = PARAM_SOURCE_STATIC,
					.steps = 0,
					.steps_source = PARAM_SOURCE_CMD_DISPENSER_DISPENSE_STEPS_UP,
					.speed = 200,
					.speed_source = PARAM_SOURCE_STATIC
				}}
			},
		.num_actions = 1
		},

		// Маркер конца рецепта
		{ .atomic_actions = NULL, .num_actions = 0 }
	};


 /**
  * @brief Рецепт: Поворот ротора реагентов.
  * Выполняется командой REAGENT_ROTATE (0x5000). added 13/02/2026
  */
 const ProcessStep_t g_recipe_reagent_rotate[] = {
		 // Шаг 1: Поворот мотора ротора реагентов
		 {.atomic_actions = (const AtomicAction_t[]){
			 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					 .motor_id = 6, // Предполагаемый ID мотора для ротора реагентов (проверить в app_config.h)
					 .motor_id_source = PARAM_SOURCE_STATIC,
					 .steps = 0,
					 .steps_source = PARAM_SOURCE_CMD_REAGENT_ROTATE_STEPS,
					 .speed = 800, // Предполагаемая скорость (проверить или сделать параметром)
					 .speed_source = PARAM_SOURCE_STATIC
				}}
			 },
			 .num_actions = 1
			},
		// Маркер конца рецепта
			{ .atomic_actions = NULL, .num_actions = 0 }
	};

 /**
  * @brief Рецепт: Перемешивание содержимого кюветы.  added 13/02/2026
  * Выполняется командой MIXER_MIX (0x3100).
  */
 const ProcessStep_t g_recipe_mixer_mix[] = {
		 // Шаг 1: Поворот миксера к кювете
		 {.atomic_actions = (const AtomicAction_t[]){
			 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					 .motor_id = 8, // Предполагаемый ID мотора для миксера (проверить в app_config.h)
					 .motor_id_source = PARAM_SOURCE_STATIC,
					 .steps = 0,
					 .steps_source = PARAM_SOURCE_CMD_MIXER_MIX_ROTATE_STEPS,
					 .speed = 1000, // Предполагаемая скорость
					 .speed_source = PARAM_SOURCE_STATIC
				}}
			 },
			 .num_actions = 1
		  },

		 // Шаг 2: Опускание лопатки миксера
		 {.atomic_actions = (const AtomicAction_t[]){
			 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					 .motor_id = 9, // ID мотора для подъема/опускания лопатки миксера
					 .motor_id_source = PARAM_SOURCE_STATIC,
					 .steps = 0,
					 .steps_source = PARAM_SOURCE_CMD_MIXER_MIX_Z_STEPS_DOWN, // Будет возвращено MIXER_Z_STEPS_DOWN
					 .speed = 200, // Предполагаемая скорость
					 .speed_source = PARAM_SOURCE_STATIC
				}}
			 },
			 .num_actions = 1
		    },

			  // Шаг 3: Включение мотора перемешивания и ожидание длительности
			  {.atomic_actions = (const AtomicAction_t[]){
				  { .action = ACTION_START_MIXING_MOTOR, .params.mixing_motor = {
						  .mixer_id = 10, // ID мотора перемешивания (физический)
						  .mixer_id_source = PARAM_SOURCE_STATIC // Logical mixer_id from command will map to this physical ID
				  }},
				  { .action = ACTION_WAIT_MS, .params.wait = {
						  .delay_ms = 0,
						  .delay_ms_source = PARAM_SOURCE_CMD_MIXER_MIX_DURATION_MS
				   }}
			  },
			  .num_actions = 2
			},

			// Шаг 4: Выключение мотора перемешивания
			{.atomic_actions = (const AtomicAction_t[]){
				{ .action = ACTION_STOP_MIXING_MOTOR, .params.mixing_motor = {
						.mixer_id = 10, // ID мотора перемешивания (физический)
						.mixer_id_source = PARAM_SOURCE_STATIC
				}}
			},
			.num_actions = 1
			},

			// Шаг 5: Подъем лопатки миксера
			{.atomic_actions = (const AtomicAction_t[]){
				{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
						.motor_id = 9, // ID мотора для подъема/опускания лопатки миксера
						.motor_id_source = PARAM_SOURCE_STATIC,
						.steps = 0,
						.steps_source = PARAM_SOURCE_CMD_MIXER_MIX_Z_STEPS_UP, // Будет возвращено MIXER_Z_STEPS_UP
						.speed = 200, // Предполагаемая скорость
						.speed_source = PARAM_SOURCE_STATIC
				}}
			},
			.num_actions = 1
			},

			// Шаг 6: Возврат миксера в исходное положение (X-Y)
			{.atomic_actions = (const AtomicAction_t[]){
				{ .action = ACTION_HOME_MOTOR, .params.home_motor = {
						.motor_id = 8, // ID мотора для поворота миксера по X-Y
						.motor_id_source = PARAM_SOURCE_STATIC,
						.speed = 400, // Скорость хоминга
						.speed_source = PARAM_SOURCE_STATIC
				}}
			},
			.num_actions = 1
			},


		// Маркер конца рецепта
			{ .atomic_actions = NULL, .num_actions = 0 }
		 };

 /**
  * @brief Рецепт: Сканирование одной кюветы фотометром. added 16/02/2026
  * Выполняется командой PHOTOMETER_SCAN_SINGLE (0x6100).
  */
 const ProcessStep_t g_recipe_photometer_scan_single[] = {
		 // Шаг 1: Поворот реакционного диска для позиционирования кюветы
		 {.atomic_actions = (const AtomicAction_t[]){
			 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					 .motor_id = 3, // ID мотора реакционного диска (предположение)
					 .motor_id_source = PARAM_SOURCE_STATIC,
					 .steps = 0,
					 .steps_source = PARAM_SOURCE_CMD_PHOTOMETER_SCAN_SINGLE_ROTATE_STEPS,
					 .speed = 1000,
					 .speed_source = PARAM_SOURCE_STATIC
				}}
			 },
			 .num_actions = 1
			 },

		// Шаг 2: Выполнение сканирования
		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_PERFORM_SCAN, .params.perform_scan = {
					.photometer_id = 1, // ID фотометра (статический)
					.photometer_id_source = PARAM_SOURCE_STATIC,
					.wavelength_mask = 0, // Будет заменено динамическим параметром
					.wavelength_mask_source = PARAM_SOURCE_CMD_PHOTOMETER_SCAN_SINGLE_WAVELENGTH_MASK
				}}
			},
			.num_actions = 1
			},

		// Маркер конца рецепта
		{ .atomic_actions = NULL, .num_actions = 0 }
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

        case RECIPE_SAMPLE_ROTATE: // <-- added 11/02/2026
        	return g_recipe_sample_rotate;

        case RECIPE_DISPENSER_ASPIRATE: // <-- added 11/02/2026
        	return g_recipe_dispenser_aspirate;

        case RECIPE_DISPENSER_DISPENSE: // <-- added 13/02/2026
        	return g_recipe_dispenser_dispense;

        case RECIPE_REAGENT_ROTATE: // <-- added 13/02/2026
        	return g_recipe_reagent_rotate;

        case RECIPE_MIXER_MIX: // <-- added 13/02/2026
        	return g_recipe_mixer_mix;

        case RECIPE_PHOTOMETER_SCAN_SINGLE: // <-- added 16/02/2026
        	return g_recipe_photometer_scan_single;




        // --- [ADD_NEW_COMMAND] ---
        // 4. Добавьте `case` для вашего нового рецепта здесь
        // case RECIPE_WASH_CUVETTE:
        //     return g_recipe_wash_cuvette;

        default:
             return NULL;
     }
 }
