#include "command_parser.h"
#include "dispatcher_io.h"
#include "host_recipe_operation.h"
#include "parameter_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "cmsis_os.h" // Required for osDelay
#include "host_direct_command_registry.h"
#include "host_recipe_command_registry.h"


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
static CommandStatus_t process_string_args_aspirate(const char *args, UniversalCommand_t *cmd);


// === Таблица строковых команд ===

static const CommandEntry_t command_table[] = {
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

static CommandStatus_t process_string_args_aspirate(const char *args, UniversalCommand_t *cmd) {
    int reagent_id;
    if (args == NULL || sscanf(args, "%d", &reagent_id) != 1) {
        return CMD_INVALID_ARGS;
    }
    cmd->args_type = ARGS_TYPE_STRING;
    strncpy(cmd->args.string, args, APP_USB_CMD_MAX_LEN - 1);
    cmd->args.string[APP_USB_CMD_MAX_LEN - 1] = '\0';
    if (HostRecipeOperation_Start(cmd) == 0) return CMD_ERROR;
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


    const HostDirectCommandDescriptor_t* direct_cmd =
            HostDirectCommandRegistry_Find(command_code);

    if (direct_cmd != NULL) {
        if (params_len < direct_cmd->min_params_len ||
                params_len > direct_cmd->max_params_len) {
            Dispatcher_SendNack(command_code, 0x0003); // ERR_INVALID_PARAMS
            return;
        }

        Dispatcher_SendAck(command_code);
        osDelay(1); // CPU, чтобы позволить USB отправить ACK до старта обработки.

        direct_cmd->handler(command_code, &packet[7], params_len);
        return;
    }

    const HostRecipeCommandDescriptor_t* recipe_cmd =
            HostRecipeCommandRegistry_Find(command_code);

    if (recipe_cmd != NULL) {
        cmd.command_code = command_code;
        cmd.recipe_id = recipe_cmd->recipe_id;

        if (params_len < recipe_cmd->min_params_len ||
                params_len > recipe_cmd->max_params_len) {
            Dispatcher_SendNack(command_code, 0x0003); // ERR_INVALID_PARAMS
            return;
        }

        Dispatcher_SendAck(command_code);
        osDelay(1); // Уступаем CPU, чтобы USB отправил ACK до старта Job'а.

        bool parse_success = Parameters_Parse(&cmd, &packet[7], params_len);
        if (!parse_success) {
            Dispatcher_SendNack(command_code, 0x0003); // ERR_INVALID_PARAMS
            return;
        }

        HostRecipeOperation_Start(&cmd);
        return;
    }


    // Если мы дошли до сюда, команда не была найдена ни в одной из таблиц.
    Dispatcher_SendError(command_code, 0x0002); // ERR_UNKNOWN_COMMAND
    return;
}
