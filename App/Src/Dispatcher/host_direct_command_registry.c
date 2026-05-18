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
        .command_code = 0x1000,
        .min_params_len = 0,
        .max_params_len = 0,
        .handler = handle_get_status
    },
    {
        .command_code = 0x1003,
        .min_params_len = 0,
        .max_params_len = 0,
        .handler = handle_get_version
    },
    {
        .command_code = 0x1010,
        .min_params_len = 0,
        .max_params_len = 0,
        .handler = handle_emergency_stop
    },
    {
        .command_code = 0x1005,
        .min_params_len = 0,
        .max_params_len = 0,
        .handler = handle_get_datetime
    },
    {
        .command_code = 0x8000,
        .min_params_len = 1,
        .max_params_len = 1,
        .handler = handle_thermo_get_temp
    },
    {
        .command_code = 0x9010,
        .min_params_len = 0,
        .max_params_len = 0,
        .handler = handle_sensor_get_all_temps
    },
    {
        .command_code = 0x9011,
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
