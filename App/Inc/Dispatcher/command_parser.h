/*
 * command_parcer.h
 *
 *  Created on: Dec 3, 2025
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_COMMAND_PARSER_H_
#define INC_DISPATCHER_COMMAND_PARSER_H_

#include <stdint.h>
#include "app_config.h"

// --- НОВЫЕ СТРУКТУРЫ ДЛЯ РАЗОБРАННЫХ ПАРАМЕТРОВ ---


/**
 * @brief Разобранные параметры для команды INIT (0x1002)
 */
typedef struct {
    uint8_t modules_mask;
} ParsedArgs_Init;

/**
 * @brief Разобранные параметры для команды DISPENSER_WASH (0x2000)
 */
typedef struct {
	uint8_t  dispenser_id;       // ID логического дозатора
	int32_t  syringe_steps;      // Рассчитанные шаги шприцевого мотора (+ аспирация, - диспенсирование) added 04-03-2026
	int32_t  rotate_steps;       // Рассчитанные шаги для поворота дозатора
	int32_t  steps_down;         // Рассчитанные шаги для опускания иглы
	int32_t  steps_up;           // Рассчитанные шаги для подъема иглы
	uint16_t cycles;             // Количество циклов промывки
} ParsedArgs_DispenserWash;


/**
 * @brief Разобранные параметры для команды WASH_STATION_WASH (0x4000) added 05/02/2026
 */
typedef struct {
	uint8_t cycles;
	uint16_t cuvette;
} ParsedArgs_WashStationWash;

/**
* @brief Разобранные параметры для команды SAMPLE_ROTATE (0x5110) added 11/02/2026
* Содержит Host slot; шаги рассчитываются позже в параметрическом слое.
*/
typedef struct {
	uint16_t slot;
	} ParsedArgs_SampleRotate;


 /**
  * @brief Разобранные параметры для команды DISPENSER_ASPIRATE (0x2100) added 11/02/2026.
  */
typedef struct {
	uint8_t  dispenser_id;       // ID логического дозатора (прямое маппирование)
	uint8_t  source_type;        // Тип источника (прямое маппирование)
	int32_t  rotate_steps;       // Рассчитанные шаги для поворота дозатора вокруг оси
	int32_t  steps_down;         // Рассчитанные шаги для опускания иглы
	int32_t  steps_up;           // Рассчитанные шаги для подъема иглы
	int32_t  syringe_steps;      // Рассчитанные шаги шприцевого мотора (+ аспирация) added 04-03-2026
	} ParsedArgs_DispenserAspirate;


 /**
  * @brief Разобранные параметры для команды DISPENSER_DISPENSE (0x2200). added 13/02/2026
  */
typedef struct {
	uint8_t  dispenser_id;       // ID логического дозатора
	uint8_t  target_type;        // Тип назначения (кювета, станция промывки и т.д.)
	int32_t  rotate_steps;       // Рассчитанные шаги для поворота дозатора
	int32_t  steps_down;         // Рассчитанные шаги для опускания иглы
	int32_t  steps_up;           // Рассчитанные шаги для подъема иглы
	int32_t  syringe_steps;      // Рассчитанные шаги шприцевого мотора (- диспенсирование) added 04-03-2026
	} ParsedArgs_DispenserDispense;

 /**
  * @brief Разобранные параметры для команды REAGENT_ROTATE (0x5000). added 13/02/2026
  */
typedef struct {
	uint8_t  rotor_id;          // ID ротора
	uint16_t slot;              // Host API: номер слота
	} ParsedArgs_ReagentRotate;

/**
 * @brief Разобранные параметры для команды MIXER_WASH (0x3000).
 */
typedef struct {
	uint8_t mixer_id;
	uint8_t cycles;
} ParsedArgs_MixerWash;


 /**
  * @brief Разобранные параметры для команды MIXER_MIX (0x3100). added 13/02/2026
  */
typedef struct {
	uint8_t   mixer_id;          // Host API: номер миксера
	uint16_t  cuvette;           // Host API: кювета для перемешивания
	uint16_t  duration_ms;       // Host API: длительность перемешивания
	uint8_t   wash_cycles;       // Host API: промывки после перемешивания
	} ParsedArgs_MixerMix;


/**
 * @brief Разобранные параметры для команды MIXER_HOME (0x3200).
 */
typedef struct {
	uint8_t mixer_id;
} ParsedArgs_MixerHome;



/**
 * @brief Разобранные параметры для команды PHOTOMETER_SCAN_SINGLE (0x6100).
 */
typedef struct {
	uint16_t cuvette;            // Host API: кювета реакционного диска
	uint8_t  wavelength_mask;    // Host API: маска длин волн
	} ParsedArgs_PhotometerScanSingle;


/**
 * @brief Разобранные параметры для команды WASH_STATION_FILL (0x4100).
 */
typedef struct {
	uint16_t volume_ul;  // Host API: volume first
  	uint16_t cuvette;    // Host API: cuvette second
} ParsedArgs_WashStationFill;



// --- КОНЕЦ НОВЫХ СТРУКТУР ---

 /**
  * @brief "Внутренние имена" для всех команд-рецептов.
  */
typedef enum {
	RECIPE_NONE = 0,
    RECIPE_ASPIRATE,
    RECIPE_INITIALIZE_SYSTEM,
	RECIPE_DISPENSER_WASH,
	RECIPE_WASH_STATION_WASH, // added 05/02/2026
	RECIPE_SAMPLE_ROTATE, // added 11/02/2026 ID для команды поворота диска образцов
	RECIPE_DISPENSER_ASPIRATE, // added 11/02/2026 ID для команды забора образца дозатором
	RECIPE_DISPENSER_DISPENSE, // added 13/02/2026
	RECIPE_REAGENT_ROTATE, // added 13/02/2026
	RECIPE_MIXER_MIX, // added 06/02/2026
	RECIPE_MIXER_WASH,// added 13/05/2026
	RECIPE_MIXER_HOME,// added 13/05/2026
	RECIPE_PHOTOMETER_SCAN_SINGLE, // <-- added 16/02/2026 НОВЫЙ ID РЕЦЕПТА ДЛЯ PHOTOMETER_SCAN_SINGLE
	RECIPE_WASH_STATION_FILL, // <-- added 17/02/2026 NEW RECIPE ID FOR WASH_STATION_FILL

	// --- [ADD_NEW_COMMAND] ---
	// 1. Добавьте новый ID рецепта здесь
	// Например: RECIPE_WASH_CUVETTE,
	RECIPE_MAX_ID
	} RecipeID_t;

// Тип указателя на функцию для обработчиков прямых команд
// Объявляется тип для указателя на функцию. Любая функция, которая соответствует этой "подписи"
// принимает uint16_t, const uint8_t*, uint16_t и ничего не возвращает),
// может быть использована как обработчик прямой команды.
// Это позволяет нам вызывать разные функции из одного и того же места в коде.

typedef void (*DirectCommandHandler_t)(uint16_t command_code, const uint8_t* params, uint16_t params_len);

// Структура-дескриптор для прямых команд
typedef struct {
	uint16_t command_code; // Код команды
	uint16_t min_params_len; // Минимальная длина параметров
	uint16_t max_params_len; // Максимальная длина параметров
	DirectCommandHandler_t  handler; // Указатель на функцию-обработчик
	} DirectCommandDescriptor_t; // "паспорт" для прямой команды.

// Структура-дескриптор для команд-рецептов
typedef struct {
	uint16_t command_code; // Код команды
	uint16_t min_params_len; // Минимальная длина параметров
	uint16_t max_params_len; // Максимальная длина параметров
	RecipeID_t recipe_id; // ID рецепта
	} RecipeCommandDescriptor_t; // "паспорт" для команды-рецепта.
		                         // Он очень похож, "паспорт" для прямой команды.
		                         // но вместо указателя на функцию-обработчик (handler)
		                         // содержит recipe_id. Это связывает код команды напрямую с ID рецепта, который должен запустить
		                         // JobManager.
 /**
  * @brief Структура для хранения бинарных аргументов.
  */
 typedef struct {
	 uint8_t raw[MAX_BINARY_ARGS_SIZE];
	 uint16_t len;
	 } BinaryArgs_t;

/**
 * @brief Универсальная структура для представления разобранной команды.
 *        Может содержать аргументы как в бинарном, так и в строковом виде.
 */

typedef struct {
	uint16_t command_code; // <-- aded command_code 22.01.2026
	RecipeID_t recipe_id;

	// Указывает, какой тип данных находится в union 'args'
	enum {
		ARGS_TYPE_NONE,   // Нет аргументов
		ARGS_TYPE_STRING, // Аргументы в виде строки
		ARGS_TYPE_BINARY,  // Аргументы в виде бинарного блока
		// --- НОВЫЙ ТИП --- 29.01.2026
		ARGS_TYPE_PARSED  // Аргументы разобраны в одну из структур ниж
		} args_type;

	union {
			char string[APP_USB_CMD_MAX_LEN];
			BinaryArgs_t binary;

			// --- НОВЫЕ ПОЛЯ --- 29.01.2026
			ParsedArgs_Init init;
			ParsedArgs_DispenserWash dispenser_wash;
			ParsedArgs_WashStationWash wash_station_wash; // <-- added 05/02/2026
			ParsedArgs_SampleRotate sample_rotate; // added 11/02/2026 Добавлено поле для поворота диска образцов
			ParsedArgs_DispenserAspirate dispenser_aspirate; // added 11/02/2026 Добавлено поле для забора образца дозатором
			ParsedArgs_DispenserDispense dispenser_dispense; // <-- added 13/02/2026
			ParsedArgs_ReagentRotate reagent_rotate; // <-- added 13/02/2026
			ParsedArgs_MixerMix mixer_mix; // <-- added 13/02/2026
			ParsedArgs_MixerWash mixer_wash;
			ParsedArgs_MixerHome mixer_home;
			ParsedArgs_PhotometerScanSingle photometer_scan_single; // <-- НОВОЕ ПОЛЕ ДЛЯ PHOTOMETER_SCAN_SINGLE added 16/02/2026
			ParsedArgs_WashStationFill wash_station_fill; // <-- added 17/02/2026 Новое поле для WASH_STATION_FILL (0x4100)
			// Здесь будут добавляться другие команды
			} args;
} UniversalCommand_t;

/**
 * @brief Обрабатывает команду, представленную в виде строки.
 * @param command_line Указатель на строку с командой.
 */
void Parser_ProcessCommand(char *command_line);


/**
 * @brief Обрабатывает команду, представленную в виде бинарного пакета.
 * @param packet Указатель на буфер с полным пакетом.
 * @param len    Общая длина пакета в буфере.
 */
void Parser_ProcessBinaryCommand(uint8_t *packet, uint16_t len);

#endif /* INC_DISPATCHER_COMMAND_PARSER_H_ */
