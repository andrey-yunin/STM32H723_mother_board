/*
 * host_recipe_operation.c
 *
 * Host recipe operation start boundary.
 *
 * Owner boundary:
 * - вызов: command_parser для Host recipe-команд;
 * - preflight: service inventory check перед INIT/recipe start;
 * - runtime: JobManager выполняет recipe steps и executor transactions;
 * - completion: JobManager возвращает JobManagerCompletionEvent_t сюда;
 * - Host lifecycle: этот модуль отправляет финальный DONE для recipe-команды.
 *
 * READY-after-INIT и emergency latch clear находятся здесь, потому что это
 * Host/system lifecycle, а не low-level recipe runtime responsibility.
 */

#include "host_recipe_operation.h"
#include "dispatcher_io.h"
#include "job_manager.h"
#include "safety_operation.h"
#include "service_manager.h"
#include "task_dispatcher.h"
#include <stddef.h>
#include <stdbool.h>

static bool HostRecipeOperation_CheckPreflight(const UniversalCommand_t* parsed_cmd)
{
    if (parsed_cmd == NULL) {
        return false;
    }

    if (parsed_cmd->recipe_id == RECIPE_INITIALIZE_SYSTEM) {
        uint8_t mask = (parsed_cmd->args_type == ARGS_TYPE_PARSED)
                ? parsed_cmd->args.init.modules_mask
                : 0xFFU;

        if (!ServiceManager_CheckInventory(mask)) {
            Dispatcher_SendError(parsed_cmd->command_code, HOST_ERR_NOT_INIT);
            Dispatcher_SendUsbResponse("ERROR: Required CAN nodes for this module are OFFLINE.");
            return false;
        }
    }

    return true;
}

static void HostRecipeOperation_HandleJobCompleted(
        const JobManagerCompletionEvent_t* event)
{
    if (event == NULL) {
        return;
    }

    Dispatcher_SendDone(event->host_command_code, event->host_status_code);

    if (event->recipe_id == RECIPE_INITIALIZE_SYSTEM && event->completed) {
        Dispatcher_SendUsbResponse("DEBUG: Signaling system READY.");
        SafetyOperation_ClearLatch();
        SetSystemReady();
    }
}

void HostRecipeOperation_Init(void)
{
    JobManager_SetCompletionCallback(HostRecipeOperation_HandleJobCompleted);
}

uint32_t HostRecipeOperation_Start(const UniversalCommand_t* parsed_cmd)
{
    if (!HostRecipeOperation_CheckPreflight(parsed_cmd)) {
        return 0U;
    }

    return JobManager_StartNewJob(parsed_cmd);
}
