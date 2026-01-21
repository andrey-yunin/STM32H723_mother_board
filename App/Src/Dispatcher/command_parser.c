#include "command_parser.h"
#include "dispatcher_io.h"
#include "job_manager.h"
#include <string.h>
#include <stdio.h>
#include "cmsis_os.h" // Required for osDelay
#include "direct_command_handlers.h"

// Таблица дескрипторов для команд-рецептов
const RecipeCommandDescriptor_t recipe_command_table[] = {
		{.command_code = 0x1002, // Код команды INIT
		 .min_params_len = 1,
		 .max_params_len = 1,
		 .recipe_id = RECIPE_INITIALIZE_SYSTEM // ID рецепта INIT
		 },

		 // Здесь будут добавляться другие команды-рецепты

		 };

// Определяем количество команд в таблице
const uint16_t RECIPE_COMMAND_TABLE_SIZE = sizeof(recipe_command_table) / sizeof(RecipeCommandDescriptor_t);





// === Локальные типы и прототипы ===

typedef enum {
    CMD_OK,
    CMD_INVALID_ARGS,
    CMD_ERROR
} CommandStatus_t;

// Указатель на функцию-обработчик строковых аргументов.
// Теперь принимает новую универсальную структуру.
typedef CommandStatus_t (*StringArgProcessor_t)(const char *args, UniversalCommand_t *cmd);

// Структура для описания одной строковой команды
typedef struct {
    const char*            command_string;
    RecipeID_t             recipe_id;
    StringArgProcessor_t   arg_processor;
    const char*            help_string;
} CommandEntry_t;

// Прототипы обработчиков строковых аргументов
static CommandStatus_t process_string_args_none(const char *args, UniversalCommand_t *cmd);
static CommandStatus_t process_string_args_motor(const char *args, UniversalCommand_t *cmd);
static CommandStatus_t process_string_args_help(const char *args, UniversalCommand_t *cmd);
static CommandStatus_t process_string_args_aspirate(const char *args, UniversalCommand_t *cmd);


// === Таблица строковых команд ===

static const CommandEntry_t command_table[] = {
    { "CMD_GET_STATUS",  RECIPE_GET_STATUS,  process_string_args_none,     "Usage: CMD_GET_STATUS" },
    { "CMD_START_MOTOR", RECIPE_START_MOTOR, process_string_args_motor,    "Usage: CMD_START_MOTOR <motor_id>" },
    { "CMD_HELP",        RECIPE_HELP,        process_string_args_help,     "Prints this help message" },
    { "CMD_ASPIRATE",    RECIPE_ASPIRATE,    process_string_args_aspirate, "Usage: CMD_ASPIRATE <reagent_id>" },
};
static const size_t num_commands = sizeof(command_table) / sizeof(command_table[0]);


// =====================================================================================
// ===                   ОБРАБОТКА СТРОКОВЫХ КОМАНД (DEBUG CONSOLE)              ===
// =====================================================================================

void Parser_ProcessCommand(char *command_line)
{
    char *command_word;
    char *arguments;
    char *saveptr;

    char line_copy[APP_USB_CMD_MAX_LEN];
    strncpy(line_copy, command_line, sizeof(line_copy) - 1);
    line_copy[sizeof(line_copy) - 1] = '\0';

    command_word = strtok_r(line_copy, " ", &saveptr);
    if (command_word == NULL) return;

    arguments = saveptr;

    for (size_t i = 0; i < num_commands; i++) {
        if (strcmp(command_word, command_table[i].command_string) == 0) {
            UniversalCommand_t cmd;
            cmd.recipe_id = command_table[i].recipe_id;

            CommandStatus_t status = command_table[i].arg_processor(arguments, &cmd);

            if (status == CMD_INVALID_ARGS) {
                char error_msg[APP_USB_RESP_MAX_LEN];
                snprintf(error_msg, sizeof(error_msg), "ERROR: Invalid arguments for '%s'. %s",
                         command_table[i].command_string, command_table[i].help_string);
                Dispatcher_SendUsbResponse(error_msg);
            }
            return;
        }
    }

    char error_msg[APP_USB_RESP_MAX_LEN];
    snprintf(error_msg, sizeof(error_msg), "ERROR: Command not found: '%s'", command_word);
    Dispatcher_SendUsbResponse(error_msg);
}

// === Реализация обработчиков строковых аргументов ===

static CommandStatus_t process_string_args_none(const char *args, UniversalCommand_t *cmd) {
    if (args != NULL && strlen(args) > 0) {
        return CMD_INVALID_ARGS;
    }
    cmd->args_type = ARGS_TYPE_NONE;
    if (JobManager_StartNewJob(cmd) == 0) return CMD_ERROR;
    return CMD_OK;
}

static CommandStatus_t process_string_args_motor(const char *args, UniversalCommand_t *cmd) {
    int motor_id;
    if (args == NULL || sscanf(args, "%d", &motor_id) != 1) {
        return CMD_INVALID_ARGS;
    }
    cmd->args_type = ARGS_TYPE_STRING;
    snprintf(cmd->args.string, APP_USB_CMD_MAX_LEN, "%d", motor_id); // Corrected: Use args instead of %d
    cmd->args.string[APP_USB_CMD_MAX_LEN - 1] = '\0';
    if (JobManager_StartNewJob(cmd) == 0) return CMD_ERROR;
    return CMD_OK;
}

static CommandStatus_t process_string_args_help(const char *args, UniversalCommand_t *cmd) {
    if (args != NULL && strlen(args) > 0) {
        return CMD_INVALID_ARGS;
    }
    Dispatcher_SendUsbResponse("--- Available String Commands ---");
    for (size_t i = 0; i < num_commands; i++) {
        Dispatcher_SendUsbResponse(command_table[i].help_string);
    }
    Dispatcher_SendUsbResponse("-------------------------------");
    return CMD_OK;
}

static CommandStatus_t process_string_args_aspirate(const char *args, UniversalCommand_t *cmd) {
    int reagent_id;
    if (args == NULL || sscanf(args, "%d", &reagent_id) != 1) {
        return CMD_INVALID_ARGS;
    }
    cmd->args_type = ARGS_TYPE_STRING;
    strncpy(cmd->args.string, args, APP_USB_CMD_MAX_LEN - 1);
    cmd->args.string[APP_USB_CMD_MAX_LEN - 1] = '\0';
    if (JobManager_StartNewJob(cmd) == 0) return CMD_ERROR;
    return CMD_OK;
}


// =====================================================================================
// ===                   ОБРАБОТКА БИНАРНЫХ КОМАНД (ОСНОВНОЙ ПРОТОКОЛ)             ===
// =====================================================================================

static uint8_t calculate_crc_parser(const uint8_t* data, uint16_t length) {
    uint8_t crc = 0;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
    }
    return crc;
}

void Parser_ProcessBinaryCommand(uint8_t *packet, uint16_t len)
{
    if (len < 8) return;

    uint16_t payload_len = (uint16_t)(packet[3] << 8) | packet[4];
    uint16_t command_code = (uint16_t)(packet[5] << 8) | packet[6];
    uint16_t total_packet_len = 3 + 2 + payload_len;

    if (total_packet_len != len) return;

    uint16_t crc_data_len = payload_len - 1;
    uint8_t calculated_crc = calculate_crc_parser(&packet[5], crc_data_len);
    uint8_t received_crc = packet[len - 1];

    if (calculated_crc != received_crc) {
        Dispatcher_SendNack(command_code, 0x0002);
        return;
    }

    Dispatcher_SendAck(command_code);
    osDelay(1); // Уступаем CPU, чтобы позволить обработчику USB отправить ACK до старта Job'а

    UniversalCommand_t cmd;
    cmd.recipe_id = RECIPE_NONE;

    uint16_t params_len = crc_data_len - 2;

    if (params_len > 0) {
        if (params_len > MAX_BINARY_ARGS_SIZE) {
             Dispatcher_SendError(command_code, 0x0005);
             return;
        }
        cmd.args_type = ARGS_TYPE_BINARY;
        memcpy(cmd.args.binary.raw, &packet[7], params_len);
        cmd.args.binary.len = params_len;
    } else {
        cmd.args_type = ARGS_TYPE_NONE;
        cmd.args.binary.len = 0;
    }



    /*
     *   * Мы добавляем цикл for, который итерирует по нашей новой таблице recipe_command_table.
   * Для каждой записи мы сравниваем command_code из входящего пакета с command_code в дескрипторе.
   * Если команда найдена:
       * Мы присваиваем cmd.recipe_id из дескриптора.
       * Выполняем проверку длины параметров, используя min_params_len и max_params_len из дескриптора. Если длина некорректна,
         отправляем NACK и выходим.
       * Запускаем JobManager_StartNewJob(&cmd).
       * После успешного запуска или обработки ошибки, мы выходим из Parser_ProcessBinaryCommand, так как команда обработана.
   * Если цикл завершился, и команда не была найдена в recipe_command_table, выполнение продолжится после этого блока, где пока еще
     находится старый switch.
     *
     */

    // Search for the command in the recipe command table
    for (uint16_t i = 0; i < RECIPE_COMMAND_TABLE_SIZE; i++) {
    	if (recipe_command_table[i].command_code == command_code) {
    		// Command found in recipe table
    		cmd.recipe_id = recipe_command_table[i].recipe_id;

    		// Parameter length validation
    		if (params_len < recipe_command_table[i].min_params_len ||
    			params_len > recipe_command_table[i].max_params_len) {
    			Dispatcher_SendNack(command_code, 0x0003); // ERR_INVALID_PARAMS
    			return;
    			}

    		// Start JobManager to execute the recipe
    		if (JobManager_StartNewJob(&cmd) == 0) {
    			Dispatcher_SendError(command_code, 0x0004); // ERR_JOB_FAILED_TO_START
    			}
    		return; // Command processed, exit function
    		}
     }


    switch (command_code)
    {
               // --- [ADD_NEW_COMMAND] ---
        // 2. Добавьте новый `case` для вашей команды здесь
        // case 0x4000: // WASH_CUVETTE
        //     cmd.recipe_id = RECIPE_WASH_CUVETTE;
        //     if (params_len != 1) {
        //         Dispatcher_SendNack(command_code, 0x0003);
        //         return;
        //     }
        //     break;
        default:
            Dispatcher_SendNack(command_code, 0x0001); // ERR_UNKNOWN_COMMAND
            return;
    }
    
    if (JobManager_StartNewJob(&cmd) == 0) {
        Dispatcher_SendError(command_code, 0x0004); // ERR_JOB_FAILED_TO_START
    }
}
