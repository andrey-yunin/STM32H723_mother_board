/*
 * command_parser.c
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#include "command_parser.h"
#include "dispatcher_io.h"
#include "Dispatcher/job_manager.h"
#include <string.h>
#include <stdio.h>

 // Внутренние определения (CommandStatus_t, ArgumentProcessor_t, CommandEntry_t)...
 // ... (они остаются такими же, как я показывал ранее)
typedef enum {
	CMD_OK,
	CMD_INVALID_ARGS,
	CMD_ERROR } CommandStatus_t;

typedef CommandStatus_t (*ArgumentProcessor_t)(const char *args, ParsedCommand_t *parsed_cmd);

typedef struct {
	const char *command_string;
	RecipeID_t initial_recipe_id;
    ArgumentProcessor_t arg_processor;
    const char *help_string;
 } CommandEntry_t;


// Прототипы обработчиков аргументов...
static CommandStatus_t process_args_get_status(const char *args, ParsedCommand_t *parsed_cmd);
static CommandStatus_t process_args_start_motor(const char *args, ParsedCommand_t *parsed_cmd);
static CommandStatus_t process_args_help(const char *args, ParsedCommand_t *parsed_cmd);
static CommandStatus_t process_args_aspirate(const char *args, ParsedCommand_t *parsed_cmd);


// Таблица команд, связывающая строки с функциями
static const CommandEntry_t command_table[] = {
     { "CMD_GET_STATUS",  RECIPE_GET_STATUS,  process_args_get_status,  "Usage: CMD_GET_STATUS" },
     { "CMD_START_MOTOR", RECIPE_START_MOTOR, process_args_start_motor, "Usage: CMD_START_MOTOR <motor_id>" },
     { "CMD_HELP",        RECIPE_HELP,        process_args_help,        "Prints this help message" },
     { "CMD_ASPIRATE",    RECIPE_ASPIRATE,    process_args_aspirate,    "Usage: CMD_ASPIRATE <reagent_id>" },
 };
 static const size_t num_commands = sizeof(command_table) / sizeof(command_table[0]);


// =====================================================================================
// ===                       ГЛАВНАЯ ФУНКЦИЯ ПАРСЕРА                             ===
// =====================================================================================
void Parser_ProcessCommand(char *command_line)
{
	char *command_word;
	char *arguments;
    char *saveptr;

    // 1. Копируем строку, чтобы не испортить оригинал
     char line_copy[APP_USB_CMD_MAX_LEN];
     strncpy(line_copy, command_line, sizeof(line_copy) - 1);
     line_copy[sizeof(line_copy) - 1] = '\0';

     // 2. РАЗБОР ФОРМАТА "КОМАНДА АРГУМЕНТЫ"
     // strtok_r находит первый пробел. Все, что до него - это command_word.
     command_word = strtok_r(line_copy, " ", &saveptr);
     if (command_word == NULL) return; // Пустая строка

     // Все, что осталось после первого пробела - это arguments.
     arguments = saveptr;

     // 3. Ищем команду в таблице
     for (size_t i = 0; i < num_commands; i++) {
         if (strcmp(command_word, command_table[i].command_string) == 0) {
             // 4. Нашли! Вызываем соответствующий обработчик аргументов
             ParsedCommand_t parsed_cmd;
             CommandStatus_t status = command_table[i].arg_processor(arguments, &parsed_cmd);

             // Обрабатываем ошибку, если обработчик сообщил о неверных аргументах
             if (status == CMD_INVALID_ARGS) {
                 char error_msg[APP_USB_RESP_MAX_LEN];
                 snprintf(error_msg, sizeof(error_msg), "ERROR: Invalid arguments for '%s'. %s",
                          command_table[i].command_string, command_table[i].help_string);
                 Dispatcher_SendUsbResponse(error_msg);
             }
             // В будущем здесь будет вызов JobManager'а
             return;
         }
     }

     // 5. Если цикл завершился, а команда не найдена
     char error_msg[APP_USB_RESP_MAX_LEN];
     snprintf(error_msg, sizeof(error_msg), "ERROR: Command not found: '%s'", command_word);
     Dispatcher_SendUsbResponse(error_msg);
 }

// =====================================================================================
// ===                   РЕАЛИЗАЦИЯ ОБРАБОТЧИКОВ АРГУМЕНТОВ                      ===
// =====================================================================================

// (Здесь находятся все функции process_args_*, которые я приводил ранее)
// ...
static CommandStatus_t process_args_get_status(const char *args, ParsedCommand_t *parsed_cmd) {
	if (args != NULL && strlen(args) > 0) {
		return CMD_INVALID_ARGS;
	    }
	parsed_cmd->recipe_id = RECIPE_GET_STATUS;
	parsed_cmd->args_buffer[0] = '\0';
	// ВЫЗЫВАЕМ JOB_MANAGER
	uint32_t job_id = JobManager_StartNewJob(parsed_cmd);
	if (job_id == 0) return CMD_ERROR; // Не удалось запустить

   return CMD_OK;
}

static CommandStatus_t process_args_start_motor(const char *args, ParsedCommand_t *parsed_cmd) {
	int motor_id;
	if (args == NULL || sscanf(args, "%d", &motor_id) != 1) {
		return CMD_INVALID_ARGS;
      }
	parsed_cmd->recipe_id = RECIPE_START_MOTOR;
	snprintf(parsed_cmd->args_buffer, APP_USB_CMD_MAX_LEN, "%d", motor_id);
	// ВЫЗЫВАЕМ JOB_MANAGER
	uint32_t job_id = JobManager_StartNewJob(parsed_cmd);
	if (job_id == 0)
		return CMD_ERROR; // Не удалось запустить

	return CMD_OK;
}

static CommandStatus_t process_args_help(const char *args, ParsedCommand_t *parsed_cmd) {
	// Команда HELP - это особая команда, которая не создает Job, а сразу выводит информацию.
	// Поэтому она не будет вызывать JobManager_StartNewJob.
	if (args != NULL && strlen(args) > 0) {
		return CMD_INVALID_ARGS;
	   }
	parsed_cmd->recipe_id = RECIPE_HELP;
	parsed_cmd->args_buffer[0] = '\0';

	Dispatcher_SendUsbResponse("--- Available Commands ---");
	for (size_t i = 0; i < num_commands; i++) {
		Dispatcher_SendUsbResponse(command_table[i].help_string);
	   }
	 Dispatcher_SendUsbResponse("------------------------");

	return CMD_OK; }


static CommandStatus_t process_args_aspirate(const char *args, ParsedCommand_t *parsed_cmd) {
	int reagent_id;
	if (args == NULL || sscanf(args, "%d", &reagent_id) != 1) {
		return CMD_INVALID_ARGS;
	   }
	parsed_cmd->recipe_id = RECIPE_ASPIRATE;
	snprintf(parsed_cmd->args_buffer, APP_USB_CMD_MAX_LEN, "%d", reagent_id);

	// ВЫЗЫВАЕМ JOB_MANAGER
	uint32_t job_id = JobManager_StartNewJob(parsed_cmd);
	if (job_id == 0)
		return CMD_ERROR; // Не удалось запустить

	return CMD_OK; }
