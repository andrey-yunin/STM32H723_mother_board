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


// Вспомогательная функция для расчета CRC (дублируется для локального использования)
static uint8_t calculate_crc_parser(const uint8_t* data, uint16_t length)
{
	uint8_t crc = 0;
	for (uint16_t i = 0; i < length; i++) {
		crc ^= data[i];
		}
	return crc;
	}

void Parser_ProcessBinaryCommand(uint8_t *packet, uint16_t len)
{
	// --- 1. Проверка базовой длины пакета ---
	// Минимальный пакет: Header(3) + Length(2) + Cmd(2) + CRC(1) = 8 байт
	if (len < 8) {
		// Пакет слишком короткий, игнорируем
		return;
		}

	// --- 2. Извлечение полей ---
	// Длина поля данных (включая команду, параметры и CRC)
	uint16_t payload_len = (uint16_t)(packet[3] << 8) | packet[4];
	// Код команды
	uint16_t command_code = (uint16_t)(packet[5] << 8) | packet[6];

	// Длина всего пакета (заголовок + длина + полезная нагрузка)
	uint16_t total_packet_len = 3 + 2 + payload_len;

	if (total_packet_len != len) {
		// Фактическая длина пакета не совпадает с заявленной, ошибка
		// В будущем можно отправлять NACK
		return;
		}

	// --- 3. Проверка CRC ---
	// CRC рассчитывается по полям: Команда + Параметры
	// Эти данные начинаются с 5-го индекса, их длина = payload_len - 1 (минус байт CRC)

	uint16_t crc_data_len = payload_len - 1;
	uint8_t calculated_crc = calculate_crc_parser(&packet[5], crc_data_len);

	// Полученный CRC - это последний байт пакета
	uint8_t received_crc = packet[len - 1];
	if (calculated_crc != received_crc) {
		// Ошибка CRC! Отправляем NACK
		// Код ошибки 0x0002 зарезервируем для CRC_ERROR
		Dispatcher_SendNack(command_code, 0x0002);
		return;
		}

	// --- 4. CRC верен, отправляем ACK ---
	Dispatcher_SendAck(command_code);

	// --- 5. Обработка команды ---
	// (Этот блок будет расширяться для запуска заданий)

	switch (command_code)
	{
		case 0x1002: // INIT
			// Здесь в будущем будет логика запуска инициализации через JobManager
			// ParsedCommand_t init_cmd = ...;
			// JobManager_StartNewJob(&init_cmd);
			break;

		// Другие case для других команд...
		 default:
			 // Неизвестная команда, но синтаксически верная.
	         // Можно отправить NACK с кодом "неизвестная команда".
			 break;
		}
}















