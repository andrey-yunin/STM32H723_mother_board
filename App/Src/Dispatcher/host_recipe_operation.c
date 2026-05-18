/*
 * host_recipe_operation.c
 *
 * Host recipe operation start boundary.
 */

#include "host_recipe_operation.h"
#include "dispatcher_io.h"
#include "job_manager.h"
#include "service_manager.h"
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
            Dispatcher_SendError(parsed_cmd->command_code, 0x0005);
            Dispatcher_SendUsbResponse("ERROR: Required CAN nodes for this module are OFFLINE.");
            return false;
        }
    }

    return true;
}

uint32_t HostRecipeOperation_Start(const UniversalCommand_t* parsed_cmd)
{
    if (!HostRecipeOperation_CheckPreflight(parsed_cmd)) {
        return 0U;
    }

    return JobManager_StartNewJob(parsed_cmd);
}
