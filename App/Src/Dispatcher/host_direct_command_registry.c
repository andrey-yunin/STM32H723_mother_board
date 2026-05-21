/*
 * host_direct_command_registry.c
 *
 *  Created on: May 18, 2026
 *      Author: andrey
 */

#include "host_direct_command_registry.h"
#include "direct_command_handlers.h"

static const HostDirectCommandDescriptor_t host_direct_command_table[] = {
	{
	    .command_code = HOST_CMD_GET_STATUS,
	    .min_params_len = 0,
	    .max_params_len = 0,
	    .handler = handle_get_status
	},
	{
	    .command_code = HOST_CMD_GET_VERSION,
	    .min_params_len = 0,
	    .max_params_len = 0,
	    .handler = handle_get_version
	},
	{
	    .command_code = HOST_CMD_EMERGENCY_STOP,
	    .min_params_len = 0,
	    .max_params_len = 0,
	    .handler = handle_emergency_stop
	},
	{
	    .command_code = HOST_CMD_GET_DATETIME,
	    .min_params_len = 0,
	    .max_params_len = 0,
	    .handler = handle_get_datetime
	},
	{
	    .command_code = HOST_CMD_THERMO_GET_TEMP,
	    .min_params_len = 1,
	    .max_params_len = 1,
	    .handler = handle_thermo_get_temp
	},
	{
	    .command_code = HOST_CMD_SENSOR_GET_ALL_TEMPS,
	    .min_params_len = 0,
	    .max_params_len = 0,
	    .handler = handle_sensor_get_all_temps
	},
	{
	    .command_code = HOST_CMD_SENSOR_GET_TEMP,
	    .min_params_len = 1,
	    .max_params_len = 1,
	    .handler = handle_sensor_get_temp
    },
};

uint16_t HostDirectCommandRegistry_Count(void)
{
    return (uint16_t)(sizeof(host_direct_command_table) /
                      sizeof(host_direct_command_table[0]));
}

const HostDirectCommandDescriptor_t* HostDirectCommandRegistry_Find(uint16_t command_code)
{
    for (uint16_t i = 0; i < HostDirectCommandRegistry_Count(); i++) {
        if (host_direct_command_table[i].command_code == command_code) {
            return &host_direct_command_table[i];
        }
    }

    return NULL;
}
