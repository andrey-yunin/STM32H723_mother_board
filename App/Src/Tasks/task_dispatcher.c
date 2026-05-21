/*
 * task_dispatcher.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#include"task_dispatcher.h"
#include "service_manager.h"
#include "cmsis_os.h"
#include "shared_resources.h"
#include "app_config.h"
#include "app_init_checker.h"
#include "Dispatcher/command_parser.h"
#include "Dispatcher/dispatcher_io.h"
#include "Dispatcher/host_direct_command_registry.h"
#include "Dispatcher/host_recipe_operation.h"
#include "Dispatcher/job_manager.h"
#include "task_watchdog.h"
#include "Dispatcher/can_response_router.h"
#include <stdbool.h>


/**
 * @brief GLOBAL SYSTEM STATE */

// Текущее состояние системы (static, чтобы быть видимым только в этом файле)

static SystemState_t g_system_state = SYS_STATE_POWER_ON;
static uint16_t g_system_error_code = HOST_ERR_OK;

static bool Dispatcher_IsCommandAllowedInErrorState(uint16_t command_code)
{
	return command_code == HOST_CMD_GET_STATUS ||
			command_code == HOST_CMD_GET_VERSION ||
			command_code == HOST_CMD_GET_DATETIME;
}

void app_start_task_dispatcher(void *argument)
{
	// Буфер для сборки одного полного пакета команды
	static uint8_t packet_buffer[APP_USB_CMD_MAX_LEN];
	// Переменные для машины состояний парсера
	uint32_t bytes_to_read = 0;
	uint32_t buffer_idx = 0;

	typedef enum {
		PARSER_STATE_WAIT_HEADER_1, // Ожидание первого байта 'C'
		PARSER_STATE_WAIT_HEADER_2, // Ожидание второго байта 'M'
		PARSER_STATE_WAIT_HEADER_3, // Ожидание третьего байта '>'
		PARSER_STATE_READ_LEN_1,    // Чтение старшего байта длины
		PARSER_STATE_READ_LEN_2,    // Чтение младшего байта длины
		PARSER_STATE_READ_PAYLOAD   // Чтение тела пакета (Команда + Параметры + CRC)
		} ParserState_t;

		ParserState_t parser_state = PARSER_STATE_WAIT_HEADER_1;

		// --- Логика инициализации системы остается без изменений ---
		if (g_system_state == SYS_STATE_POWER_ON)
			{
			g_system_state = SYS_STATE_INITIALIZING;
			Dispatcher_SendUsbResponse("INFO: System starting. Initializing hardware...");
			
			// Инициализация runtime-состояния менеджеров до discovery и первого Host job.
			// Router должен быть очищен до приема service-ответов F001/F004/F007.

			CanResponseRouter_Init();
			ServiceManager_Init();
			JobManager_Init();


			for (uint8_t i = 0; i < 5; i++) {
				ServiceManager_StartDiscovery();
				osDelay(250);
			}

			// Создаем универсальную команду для инициализации
			UniversalCommand_t init_cmd;
			init_cmd.command_code = 0x1002;
			init_cmd.recipe_id = RECIPE_INITIALIZE_SYSTEM;
			init_cmd.args_type = ARGS_TYPE_NONE; // Инициализация не требует аргументов

			uint32_t init_job_id = HostRecipeOperation_Start(&init_cmd);

			if (init_job_id == 0) {
				Dispatcher_SendUsbResponse("CRITICAL ERROR: Failed to start system initialization job!");
				SetSystemError(HOST_ERR_NOT_INIT);
				}
			osDelay(100);
			}

	// --- НОВЫЙ ГЛАВНЫЙ ЦИКЛ ЗАДАЧИ ---
	for(;;)
		{
		// Пытаемся прочитать 1 байт из буфера потока.
		// Блокируемся на время portMAX_DELAY, если данных нет.
		uint8_t current_byte;
		Watchdog_Kick(TASK_ID_DISPATCHER);
		size_t bytes_read = xStreamBufferReceive(usb_rx_stream_buffer_handle,
				(void*)&current_byte, 1, pdMS_TO_TICKS(1000));

		if (bytes_read > 0)
			{
				switch (parser_state)
				{
					case PARSER_STATE_WAIT_HEADER_1:
						if (current_byte == 0x43) { // 'C'
							packet_buffer[0] = current_byte;
							parser_state = PARSER_STATE_WAIT_HEADER_2;
							}
						break;

					case PARSER_STATE_WAIT_HEADER_2:
						if (current_byte == 0x4D) { // 'M'
							packet_buffer[1] = current_byte;
							parser_state = PARSER_STATE_WAIT_HEADER_3;
							}
						else {
							parser_state = PARSER_STATE_WAIT_HEADER_1; // Ошибка, начинаем сначала
							}
						break;

					case PARSER_STATE_WAIT_HEADER_3:
						if (current_byte == 0x3E) { // '>'
							packet_buffer[2] = current_byte;
							parser_state = PARSER_STATE_READ_LEN_1;
							}
						else {
							parser_state = PARSER_STATE_WAIT_HEADER_1; // Ошибка, начинаем сначала
							}
						break;

					case PARSER_STATE_READ_LEN_1:
						packet_buffer[3] = current_byte;
						bytes_to_read = (uint32_t)current_byte << 8; // Старший байт
						parser_state = PARSER_STATE_READ_LEN_2;
						break;

					case PARSER_STATE_READ_LEN_2:
						packet_buffer[4] = current_byte;
						bytes_to_read |= current_byte; // Младший байт

						// Проверка на максимальную длину пакета
						if (bytes_to_read > 0 && (bytes_to_read + 5) <= APP_USB_CMD_MAX_LEN) {
							buffer_idx = 5; // Начинаем запись с 5-го индекса
							parser_state = PARSER_STATE_READ_PAYLOAD;
							}
						else {
							parser_state = PARSER_STATE_WAIT_HEADER_1; // Неверная длина, сброс
							}
						break;

					case PARSER_STATE_READ_PAYLOAD:
						packet_buffer[buffer_idx++] = current_byte;
						bytes_to_read--;
						if (bytes_to_read == 0)
							{
							// Пакет собран!
							if (g_system_state == SYS_STATE_READY || g_system_state == SYS_STATE_INITIALIZING)
							{
								// Передаем собранный бинарный пакет в обработчик
								// (эту функцию мы создадим на следующем шаге)
								Parser_ProcessBinaryCommand(packet_buffer, buffer_idx);
								}
							else {
								if (buffer_idx >= 7U) {
									uint16_t command_code =
											(uint16_t)(((uint16_t)packet_buffer[5] << 8) |
													packet_buffer[6]);
									if (Dispatcher_IsCommandAllowedInErrorState(command_code)) {
										Parser_ProcessBinaryCommand(packet_buffer, buffer_idx);
									}
									else {
										Dispatcher_SendError(command_code, g_system_error_code);
									}
								}
								}
							// Сброс состояния для ожидания нового пакета
							parser_state = PARSER_STATE_WAIT_HEADER_1;
							}
						break;
						}
				}
		}
}


// Эта функция будет вызываться из JobManager'а, когда инициализация завершена.
// Для этого нам понадобится механизм межзадачного взаимодействия (например, Event Group).
void SetSystemReady(void)
{
	g_system_state = SYS_STATE_READY;
	g_system_error_code = HOST_ERR_OK;
}

void SetSystemBusy(void)
{
	g_system_state = SYS_STATE_BUSY;
	g_system_error_code = HOST_ERR_BUSY;
}

/**
 *
 * @brief Возвращает текущее глобальное состояние системы.
 * @param None
 * @retval Текущее состояние системы (SystemState_t).
 */
SystemState_t GetSystemState(void)
{
	return g_system_state;
}

uint16_t GetSystemErrorCode(void)
{
	return g_system_error_code;
}

void SetSystemError(uint16_t error_code)
{
	g_system_state = SYS_STATE_ERROR;
	g_system_error_code = (error_code != HOST_ERR_OK)
			? error_code
			: HOST_ERR_GENERAL;
}
