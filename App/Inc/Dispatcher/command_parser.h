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

 /**
  * @brief "Внутренние имена" для всех команд-рецептов.
  */
typedef enum {
	RECIPE_NONE = 0,
    RECIPE_START_MOTOR,
    RECIPE_ASPIRATE,
    RECIPE_INITIALIZE_SYSTEM,

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
	uint16_t command_code; // <-- Добавлено поле command_code 22.01.2026
	RecipeID_t recipe_id;

	// Указывает, какой тип данных находится в union 'args'
	enum {
		ARGS_TYPE_NONE,   // Нет аргументов
		ARGS_TYPE_STRING, // Аргументы в виде строки
		ARGS_TYPE_BINARY  // Аргументы в виде бинарного блока
		} args_type;

	union {
			char string[APP_USB_CMD_MAX_LEN];
			BinaryArgs_t binary;
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
