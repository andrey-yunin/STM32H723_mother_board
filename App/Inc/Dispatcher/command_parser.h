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
     RECIPE_GET_STATUS,
     RECIPE_START_MOTOR,
     RECIPE_HELP,
     RECIPE_ASPIRATE,
         RECIPE_INITIALIZE_SYSTEM,
         // --- [ADD_NEW_COMMAND] ---
         // 1. Добавьте новый ID рецепта здесь
         // Например: RECIPE_WASH_CUVETTE,
         RECIPE_MAX_ID
      } RecipeID_t;

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
