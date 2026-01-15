/*
 * task_dispatcher.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#include"task_dispatcher.h"
#include "cmsis_os.h"
#include "shared_resources.h"
#include "app_config.h"
#include "Dispatcher/command_parser.h"
#include "Dispatcher/dispatcher_io.h"
#include "Dispatcher/job_manager.h"

/**
 * @brief GLOBAL SYSTEM STATE
 */
typedef enum {
	SYS_STATE_POWER_ON,       // Начальное состояние после включения
    SYS_STATE_INITIALIZING,   // Идет процесс инициализации (homing)
    SYS_STATE_READY,          // Система готова к приему команд
    SYS_STATE_BUSY,           // Система выполняет комплексную команду (определяется JobManager'ом)
    SYS_STATE_ERROR           // Произошла ошибка, требуется вмешательство
 } SystemState_t;

// Текущее состояние системы (static, чтобы быть видимым только в этом файле)

static SystemState_t g_system_state = SYS_STATE_POWER_ON;

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
			// Создаем универсальную команду для инициализации
			UniversalCommand_t init_cmd;
			init_cmd.recipe_id = RECIPE_INITIALIZE_SYSTEM;
			init_cmd.args_type = ARGS_TYPE_NONE; // Инициализация не требует аргументов

			uint32_t init_job_id = JobManager_StartNewJob(&init_cmd);

			if (init_job_id == 0) {
				Dispatcher_SendUsbResponse("CRITICAL ERROR: Failed to start system initialization job!");
				g_system_state = SYS_STATE_ERROR;
				}
			osDelay(100);
			}

	// --- НОВЫЙ ГЛАВНЫЙ ЦИКЛ ЗАДАЧИ ---
	for(;;)
		{
		// Пытаемся прочитать 1 байт из буфера потока.
		// Блокируемся на время portMAX_DELAY, если данных нет.
		uint8_t current_byte;
		size_t bytes_read = xStreamBufferReceive(usb_rx_stream_buffer_handle,
				(void*)&current_byte, 1, portMAX_DELAY);

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
								Dispatcher_SendUsbResponse("ERROR: System is not ready for binary commands.");
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
}




