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
	char rx_buffer[APP_USB_CMD_MAX_LEN];

    // --- АВТОМАТИЧЕСКИЙ ЗАПУСК ИНИЦИАЛИЗАЦИИ ---
    if (g_system_state == SYS_STATE_POWER_ON)
    	{
    	g_system_state = SYS_STATE_INITIALIZING;
        Dispatcher_SendUsbResponse("INFO: System starting. Initializing hardware...");
        // Запускаем настоящее задание инициализации через JobManager
        ParsedCommand_t init_cmd = {.recipe_id = RECIPE_INITIALIZE_SYSTEM};
        init_cmd.args_buffer[0] = '\0';
        uint32_t init_job_id = JobManager_StartNewJob(&init_cmd);

        if (init_job_id == 0) {
        	Dispatcher_SendUsbResponse("CRITICAL ERROR: Failed to start system initialization job!");
            g_system_state = SYS_STATE_ERROR;
                     }
            // Теперь мы не симулируем завершение, а ждем, пока JobManager его обработает.
            // JobManager должен будет изменить g_system_state на SYS_STATE_READY.
            // Пока мы не реализовали этот механизм обратной связи, система останется в состоянии INITIALIZING.
            // Чтобы протестировать, давайте временно установим его вручную после небольшой задержки.
            osDelay(100); // Даем время job_manager'у запустить задание
            // g_system_state = SYS_STATE_READY; // Эту строку нужно будет убрать в будущем
        }


       for(;;)
    	   {
    	   // Ждем команду из очереди usb_rx_queue
    	   if (xQueueReceive(usb_rx_queue_handle, (void *)rx_buffer, portMAX_DELAY) == pdPASS)
    		   {
    		   // Обрабатываем команды только если система готова
               // Временно разрешим команды даже во время инициализации для теста
    		   if (g_system_state == SYS_STATE_READY || g_system_state == SYS_STATE_INITIALIZING)
    			   {
    			   // Передаем команду в наш модульный обработчик
    			   Parser_ProcessCommand(rx_buffer);
    			   }
    		   else
    			   {
    			   Dispatcher_SendUsbResponse("ERROR: System is in an error or busy state. Commands not accepted.");
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




