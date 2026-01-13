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
     RECIPE_MAX_ID
 } RecipeID_t;

 /**
  * @brief Структура для передачи разобранной команды.
  */
 typedef struct {
     RecipeID_t  recipe_id;
     char        args_buffer[APP_USB_CMD_MAX_LEN];
 } ParsedCommand_t;

 /**
  * @brief Главная функция парсера.
  *        Принимает строку формата "КОМАНДА АРГУМЕНТ1 АРГУМЕНТ2...",
  *        находит нужный обработчик и вызывает его.
  *
  * @param command_line Указатель на строку с командой.
  */
 void Parser_ProcessCommand(char *command_line);

 /**
  * @brief Новая функция для обработки бинарных пакетов.
  *        Принимает сырой бинарный пакет, проверяет его и инициирует выполнение.
  *
  * @param packet Указатель на буфер с полным пакетом (включая заголовок).
  * @param len    Общая длина пакета в буфере.
  */
void Parser_ProcessBinaryCommand(uint8_t *packet, uint16_t len); // <-- ДОБАВИТЬ ЭТУ СТРОКУ




#endif /* INC_DISPATCHER_COMMAND_PARSER_H_ */
