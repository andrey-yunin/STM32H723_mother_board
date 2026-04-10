/*
 * recipe_store.c
 *
 *  Created on: Dec 3, 2025
 *      Author: andrey
 */

#include "recipe_store.h"
#include <stddef.h> // Для NULL
#include <system_mapping.h>
#include "param_translator.h" // Для SYRINGE_DEFAULT_SPEED

 // ============================================================================
 // ---                  ХРАНИЛИЩЕ РЕЦЕПТОВ (во Flash-памяти)                ---
 // ============================================================================

 /**
  * @brief Рецепт: Инициализация всей системы (Homing).
  *
  * Выполняется один раз при старте для приведения механизмов в известное положение.
  */
 const ProcessStep_t g_recipe_initialize_system[] = {
     // Шаг 1: Поиск "дома" для иглы дозатора (Z-ось).
     {
         .atomic_actions = (const AtomicAction_t[]){
             { .action = ACTION_HOME_MOTOR, .params.home_motor = { .motor_id=SYS_DISPENSER_MOTOR_Z, .speed=150 } }
         },
         .num_actions = 1
     },

     // Шаг 2: Поиск "дома" для дозатора (X-Y ось).
     {
         .atomic_actions = (const AtomicAction_t[]){
             { .action = ACTION_HOME_MOTOR, .params.home_motor = { .motor_id=SYS_DISPENSER_MOTOR_XY, .speed=400 } }
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
     // Шаг 1: Поворот дозатора (X-Y ось) к пробирке.
     {
         .atomic_actions = (const AtomicAction_t[]){
             { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = { .motor_id=SYS_DISPENSER_MOTOR_XY, .steps=1000, .speed=500 } }
         },
         .num_actions = 1
     },

     // Шаг 2: Опускание иглы (Z-ось).
     {
         .atomic_actions = (const AtomicAction_t[]){
             { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = { .motor_id=SYS_DISPENSER_MOTOR_Z, .steps=200, .speed=100 } }
         },
         .num_actions = 1
     },

	 // Шаг 3: Работа шприцевого мотора (аспирация, статические параметры).
	 {
			.atomic_actions = (const AtomicAction_t[]){
				{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
						.motor_id = SYS_DISPENSER_MOTOR_SYRINGE,
	                    .motor_id_source = PARAM_SOURCE_STATIC,
	                    .steps = 500,
	                    .steps_source = PARAM_SOURCE_STATIC,
	                    .speed = SYRINGE_DEFAULT_SPEED,
	                    .speed_source = PARAM_SOURCE_STATIC
	                }}
	            },
	            .num_actions = 1
	        },


     // Шаг 5: Поднятие иглы (Z-ось, обратное направление).
     {
         .atomic_actions = (const AtomicAction_t[]){
             { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = { .motor_id=SYS_DISPENSER_MOTOR_Z, .steps=-200, .speed=100 } }
         },
         .num_actions = 1
     },

     // Шаг 6: Возврат дозатора (X-Y ось, обратное направление).
     {
         .atomic_actions = (const AtomicAction_t[]){
             { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = { .motor_id=SYS_DISPENSER_MOTOR_XY, .steps=-1000, .speed=500 } }
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
		 // Шаг 1: Поворот дозатора (X-Y ось) к промывочной станции.
		 {.atomic_actions = (const AtomicAction_t[]){
			 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					 .motor_id = SYS_DISPENSER_MOTOR_XY,
					 .motor_id_source = PARAM_SOURCE_STATIC,
					 .steps = 0, // Placeholder, will be replaced by source
					 .steps_source = PARAM_SOURCE_CMD_DISPENSER_WASH_ROTATE_STEPS,
					 .speed = 800,
					 .speed_source = PARAM_SOURCE_STATIC
				}}
			 },
			 .num_actions = 1
			 },

		 // Шаг 2: Опускание иглы (Z-ось дозатора) в промывочную станцию.
			 {.atomic_actions = (const AtomicAction_t[]){
				 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					 .motor_id = SYS_DISPENSER_MOTOR_Z,
					 .motor_id_source = PARAM_SOURCE_STATIC,
					 .steps = 0,
					 .steps_source = PARAM_SOURCE_CMD_DISPENSER_WASH_STEPS_DOWN,
					 .speed = 400,
					 .speed_source = PARAM_SOURCE_STATIC
				}}
			 },
			 .num_actions = 1
			 },


			 // Шаг 3: Работа шприцевого мотора (диспенсирование промывочной жидкости).
			 {.atomic_actions = (const AtomicAction_t[]){
				 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
						 .motor_id = SYS_DISPENSER_MOTOR_SYRINGE,
			             .motor_id_source = PARAM_SOURCE_STATIC,
			             .steps = 0,
			             .steps_source = PARAM_SOURCE_CMD_DISPENSER_WASH_SYRINGE_STEPS,
			             .speed = SYRINGE_DEFAULT_SPEED,
			             .speed_source = PARAM_SOURCE_STATIC
					}}
				 },
				 .num_actions = 1
				 },



			 // Шаг 4: Поднятие иглы (Z-ось дозатора).
			 {.atomic_actions = (const AtomicAction_t[]){
				 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					   .motor_id = SYS_DISPENSER_MOTOR_Z,
					   .motor_id_source = PARAM_SOURCE_STATIC,
					   .steps = 0,
					   .steps_source = PARAM_SOURCE_CMD_DISPENSER_WASH_STEPS_UP,
					   .speed = 400,
					   .speed_source = PARAM_SOURCE_STATIC
				  }}
			  },
			  .num_actions = 1
			  },


			  // Шаг 5: Возврат дозатора (X-Y ось) в исходное положение.
			  {.atomic_actions = (const AtomicAction_t[]){
				  { .action = ACTION_HOME_MOTOR,
					    .params.home_motor = {
						.motor_id = SYS_DISPENSER_MOTOR_XY,
						.motor_id_source = PARAM_SOURCE_STATIC,
						.speed = 800,
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
		// Шаг 1: Поворот реакционного диска к моющей станции.
		{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_ROTATE_MOTOR,
						.params.rotate_motor = {
							.motor_id = SYS_REACTION_DISK_MOTOR, .motor_id_source = PARAM_SOURCE_STATIC,
							.steps = 3000, .steps_source = PARAM_SOURCE_CMD_WASH_STATION_WASH_ROTATE_STEPS, // Шаги берутся из рассчитанного параметра
							.speed = 1000, .speed_source = PARAM_SOURCE_STATIC // Скорость фиксирована
					}}
				},
					.num_actions = 1
					},

		// Шаг 2: Заполнение кюветы (насос заполнения).
    	{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_START_PUMP,
						.params.pump = { .pump_id = SYS_WASH_PUMP_FILL, .pump_id_source = PARAM_SOURCE_STATIC } },
					{ .action = ACTION_WAIT_MS,    .params.wait = { .delay_ms = 1000, .delay_ms_source = PARAM_SOURCE_STATIC }} // Задержка фиксирована
					},
					.num_actions = 2
					},

		// Шаг 3: Остановка насоса заполнения.
		{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_STOP_PUMP, .params.pump = { .pump_id = SYS_WASH_PUMP_FILL, .pump_id_source = PARAM_SOURCE_STATIC } }
					},
					.num_actions = 1
					},

		// Шаг 4: Слив из кюветы (насос слива).
		{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_START_PUMP, .params.pump = { .pump_id = SYS_WASH_PUMP_DRAIN, .pump_id_source = PARAM_SOURCE_STATIC } },
					{ .action = ACTION_WAIT_MS, .params.wait = { .delay_ms = 1000, .delay_ms_source = PARAM_SOURCE_STATIC } }
					},
					.num_actions = 2
					},

		// Шаг 5: Остановка насоса слива.
		{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_STOP_PUMP, .params.pump = { .pump_id = SYS_WASH_PUMP_DRAIN, .pump_id_source = PARAM_SOURCE_STATIC } }
					},
					.num_actions = 1
					},

		// Маркер конца рецепта.
		{
				.atomic_actions = NULL, .num_actions = 0 }
		};


/**
 * @brief Рецепт: Заполнение кюветы на моющей станции (WASH_STATION_FILL). added 17/02/2026
 * Выполняется командой WASH_STATION_FILL (0x4100).
 */
 const ProcessStep_t g_recipe_wash_station_fill[] = {
		 // Шаг 1: Поворот реакционного диска к кювете для заполнения.
		 {.atomic_actions = (const AtomicAction_t[]){
			 { .action = ACTION_ROTATE_MOTOR,
				 .params.rotate_motor = {
						 .motor_id = SYS_REACTION_DISK_MOTOR,
						 .motor_id_source = PARAM_SOURCE_STATIC,
						 .steps = 0,
						 .steps_source = PARAM_SOURCE_CMD_WASH_STATION_FILL_ROTATE_STEPS, // Шаги берутся из рассчитанного параметра
						 .speed = 1000, .speed_source = PARAM_SOURCE_STATIC // Скорость фиксирована
					}}
			 },
			 .num_actions = 1
			 },

		  // Шаг 2: Запуск насоса заполнения и ожидание.
		  {.atomic_actions = (const AtomicAction_t[]){
			  { .action = ACTION_START_PUMP,
				  .params.pump = {
						  .pump_id = SYS_WASH_PUMP_FILL,
						  .pump_id_source = PARAM_SOURCE_STATIC
					  }},
			   { .action = ACTION_WAIT_MS,
				   .params.wait = {
						   .delay_ms = 0, // Будет заменено динамическим параметром
						   .delay_ms_source = PARAM_SOURCE_CMD_WASH_STATION_FILL_PUMP_DURATION_MS
					 }}
				},
				.num_actions = 2
				},


			// Шаг 3: Остановка насоса заполнения.
			{.atomic_actions = (const AtomicAction_t[]){
				{ .action = ACTION_STOP_PUMP,
					.params.pump = {
							.pump_id = SYS_WASH_PUMP_FILL,
							.pump_id_source = PARAM_SOURCE_STATIC
				      }}
				},
				.num_actions = 1
				},

		    // Маркер конца рецепта.
				{ .atomic_actions = NULL, .num_actions = 0 }

		 };


// Определение шагов для рецепта SAMPLE_ROTATE (0x5110) added 11/02/2026

const ProcessStep_t g_recipe_sample_rotate[] = {
		{ .atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					.motor_id = SYS_REAGENT_SAMPLE_DISK_MOTOR,
					.motor_id_source = PARAM_SOURCE_STATIC,
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
		// Шаг 1: Поворот дозатора (X-Y ось) к источнику.

		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					.motor_id = SYS_DISPENSER_MOTOR_XY,
					.motor_id_source = PARAM_SOURCE_STATIC,
					.steps = 0,
					.steps_source = PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_ROTATE_STEPS,
					.speed = 500,
					.speed_source = PARAM_SOURCE_STATIC
					}}
				},
				.num_actions = 1 // Одно движение
		},


		// Шаг 2: Опускание иглы (Z-ось дозатора).

		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					.motor_id = SYS_DISPENSER_MOTOR_Z,
					.motor_id_source = PARAM_SOURCE_STATIC,
					.steps = 0,
					.steps_source = PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_STEPS_DOWN,
					.speed = 200,
					.speed_source = PARAM_SOURCE_STATIC
					}}
				},
				.num_actions = 1 // Одно движение
		},


		// Шаг 3: Работа шприцевого мотора (аспирация жидкости). added 04/03/2026
		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					.motor_id = SYS_DISPENSER_MOTOR_SYRINGE,
		            .motor_id_source = PARAM_SOURCE_STATIC,
		            .steps = 0,
		            .steps_source = PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_SYRINGE_STEPS,
		            .speed = SYRINGE_DEFAULT_SPEED,
		            .speed_source = PARAM_SOURCE_STATIC
					}}
				},
				.num_actions = 1
		},




			 // Шаг 4: Подъем иглы (Z-ось дозатора).
			{
				.atomic_actions = (const AtomicAction_t[]){
					{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
						.motor_id = SYS_DISPENSER_MOTOR_Z,
						.motor_id_source = PARAM_SOURCE_STATIC,
						.steps = 0,
						.steps_source = PARAM_SOURCE_CMD_DISPENSER_ASPIRATE_STEPS_UP,
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
 * @brief Рецепт: Выдача образца/реагента дозатором.  added 13/02/2026
 * Выполняется командой DISPENSER_DISPENSE (0x2200).
 */
 const ProcessStep_t g_recipe_dispenser_dispense[] = {
		 // Шаг 1: Поворот дозатора (X-Y ось) к цели.
		 { .atomic_actions = (const AtomicAction_t[]){
			 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					 .motor_id = SYS_DISPENSER_MOTOR_XY,
					 .motor_id_source = PARAM_SOURCE_STATIC,
					 .steps = 0,
					 .steps_source = PARAM_SOURCE_CMD_DISPENSER_DISPENSE_ROTATE_STEPS,
					 .speed = 500,
					 .speed_source = PARAM_SOURCE_STATIC
				}}
			 },
			 .num_actions = 1
		},

		// Шаг 2: Опускание иглы (Z-ось дозатора).
		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					.motor_id = SYS_DISPENSER_MOTOR_Z,
					.motor_id_source = PARAM_SOURCE_STATIC,
					.steps = 0,
					.steps_source = PARAM_SOURCE_CMD_DISPENSER_DISPENSE_STEPS_DOWN,
					.speed = 200,
					.speed_source = PARAM_SOURCE_STATIC
				}}
			},
			.num_actions = 1
		},

		// Шаг 3: Работа шприцевого мотора (диспенсирование жидкости).
		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					.motor_id = SYS_DISPENSER_MOTOR_SYRINGE,
		            .motor_id_source = PARAM_SOURCE_STATIC,
		            .steps = 0,
		            .steps_source = PARAM_SOURCE_CMD_DISPENSER_DISPENSE_SYRINGE_STEPS,
		            .speed = SYRINGE_DEFAULT_SPEED,
		            .speed_source = PARAM_SOURCE_STATIC
				}}
			},
			.num_actions = 1
		},


		// Шаг 4: Подъем иглы (Z-ось дозатора).
		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					.motor_id = SYS_DISPENSER_MOTOR_Z,
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
		 // Шаг 1: Поворот диска реагентов/образцов.
		 {.atomic_actions = (const AtomicAction_t[]){
			 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					 .motor_id = SYS_REAGENT_SAMPLE_DISK_MOTOR,
					 .motor_id_source = PARAM_SOURCE_STATIC,
					 .steps = 0,
					 .steps_source = PARAM_SOURCE_CMD_REAGENT_ROTATE_STEPS,
					 .speed = 800,
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
		 // Шаг 1: Поворот миксера (X-Y ось) к кювете.
		 {.atomic_actions = (const AtomicAction_t[]){
			 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					 .motor_id = SYS_MIXER_MOTOR_XY,
					 .motor_id_source = PARAM_SOURCE_STATIC,
					 .steps = 0,
					 .steps_source = PARAM_SOURCE_CMD_MIXER_MIX_ROTATE_STEPS,
					 .speed = 1000,
					 .speed_source = PARAM_SOURCE_STATIC
				}}
			 },
			 .num_actions = 1
		  },

		 // Шаг 2: Опускание лопатки миксера (Z-ось).
		 {.atomic_actions = (const AtomicAction_t[]){
			 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					 .motor_id = SYS_MIXER_MOTOR_Z,
					 .motor_id_source = PARAM_SOURCE_STATIC,
					 .steps = 0,
					 .steps_source = PARAM_SOURCE_CMD_MIXER_MIX_Z_STEPS_DOWN,
					 .speed = 200,
					 .speed_source = PARAM_SOURCE_STATIC
				}}
			 },
			 .num_actions = 1
		    },

			  // Шаг 3: Включение мотора перемешивания (лопатка) и ожидание.
			  {.atomic_actions = (const AtomicAction_t[]){
				  { .action = ACTION_START_MIXING_MOTOR, .params.mixing_motor = {
						  .mixer_id = SYS_MIXER_PADDLE_MOTOR,
						  .mixer_id_source = PARAM_SOURCE_STATIC
				  }},
				  { .action = ACTION_WAIT_MS, .params.wait = {
						  .delay_ms = 0,
						  .delay_ms_source = PARAM_SOURCE_CMD_MIXER_MIX_DURATION_MS
				   }}
			  },
			  .num_actions = 2
			},

			// Шаг 4: Выключение мотора перемешивания (лопатка).
			{.atomic_actions = (const AtomicAction_t[]){
				{ .action = ACTION_STOP_MIXING_MOTOR, .params.mixing_motor = {
						.mixer_id = SYS_MIXER_PADDLE_MOTOR,
						.mixer_id_source = PARAM_SOURCE_STATIC
				}}
			},
			.num_actions = 1
			},

			// Шаг 5: Подъем лопатки миксера (Z-ось).
			{.atomic_actions = (const AtomicAction_t[]){
				{ .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
						.motor_id = SYS_MIXER_MOTOR_Z,
						.motor_id_source = PARAM_SOURCE_STATIC,
						.steps = 0,
						.steps_source = PARAM_SOURCE_CMD_MIXER_MIX_Z_STEPS_UP,
						.speed = 200,
						.speed_source = PARAM_SOURCE_STATIC
				}}
			},
			.num_actions = 1
			},

			// Шаг 6: Возврат миксера (X-Y ось) в исходное положение.
			{.atomic_actions = (const AtomicAction_t[]){
				{ .action = ACTION_HOME_MOTOR, .params.home_motor = {
						.motor_id = SYS_MIXER_MOTOR_XY,
						.motor_id_source = PARAM_SOURCE_STATIC,
						.speed = 400,
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
		 // Шаг 1: Поворот реакционного диска для позиционирования кюветы.
		 {.atomic_actions = (const AtomicAction_t[]){
			 { .action = ACTION_ROTATE_MOTOR, .params.rotate_motor = {
					 .motor_id = SYS_REACTION_DISK_MOTOR,
					 .motor_id_source = PARAM_SOURCE_STATIC,
					 .steps = 0,
					 .steps_source = PARAM_SOURCE_CMD_PHOTOMETER_SCAN_SINGLE_ROTATE_STEPS,
					 .speed = 1000,
					 .speed_source = PARAM_SOURCE_STATIC
				}}
			 },
			 .num_actions = 1
			 },

		// Шаг 2: Выполнение сканирования.
		{.atomic_actions = (const AtomicAction_t[]){
			{ .action = ACTION_PERFORM_SCAN, .params.perform_scan = {
					.photometer_id = SYS_PHOTOMETER_MAIN,
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

        case RECIPE_WASH_STATION_FILL: // <-- added 17/02/2026 Новое: Заполнение моющей станции
        	return g_recipe_wash_station_fill;





        // --- [ADD_NEW_COMMAND] ---
        // 4. Добавьте `case` для вашего нового рецепта здесь
        // case RECIPE_WASH_CUVETTE:
        //     return g_recipe_wash_cuvette;

        default:
             return NULL;
     }
 }
