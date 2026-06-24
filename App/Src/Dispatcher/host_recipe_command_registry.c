/*
 * host_recipe_command_registry.c
 *
 *  Created on: May 18, 2026
 *      Author: andrey
 */

#include "host_recipe_command_registry.h"

static const HostRecipeCommandDescriptor_t host_recipe_command_table[] = {
    {
        .command_code = 0x1002,
        .min_params_len = 1,
        .max_params_len = 1,
        .recipe_id = RECIPE_INITIALIZE_SYSTEM
    },
    {
        .command_code = 0x2000,
        .min_params_len = 4,
        .max_params_len = 4,
        .recipe_id = RECIPE_DISPENSER_WASH
    },
    {
        .command_code = 0x2100,
        .min_params_len = 6,
        .max_params_len = 6,
        .recipe_id = RECIPE_DISPENSER_ASPIRATE
    },
    {
        .command_code = 0x2200,
        .min_params_len = 6,
        .max_params_len = 6,
        .recipe_id = RECIPE_DISPENSER_DISPENSE
    },
    {
        .command_code = 0x3100,
        .min_params_len = 6,
        .max_params_len = 6,
        .recipe_id = RECIPE_MIXER_MIX
    },
    {
        .command_code = 0x4000,
        .min_params_len = 3,
        .max_params_len = 3,
        .recipe_id = RECIPE_WASH_STATION_WASH
    },
    {
        .command_code = 0x4100,
        .min_params_len = 4,
        .max_params_len = 4,
        .recipe_id = RECIPE_WASH_STATION_FILL
    },
    {
        .command_code = 0x5000,
        .min_params_len = 3,
        .max_params_len = 3,
        .recipe_id = RECIPE_REAGENT_ROTATE
    },
    {
        .command_code = 0x5110,
        .min_params_len = 2,
        .max_params_len = 2,
        .recipe_id = RECIPE_SAMPLE_ROTATE
    },
    {
        .command_code = 0x6000,
        .min_params_len = 1,
        .max_params_len = 1,
        .recipe_id = RECIPE_PHOTOMETER_SCAN_ALL
    },
    {
        .command_code = 0x6100,
        .min_params_len = 3,
        .max_params_len = 3,
        .recipe_id = RECIPE_PHOTOMETER_SCAN_SINGLE
    },
    {
        .command_code = 0x6200,
        .min_params_len = 2,
        .max_params_len = 2,
        .recipe_id = RECIPE_PHOTOMETER_CALIBRATE
    },
    {
        .command_code = 0x6300,
        .min_params_len = 0,
        .max_params_len = 0,
        .recipe_id = RECIPE_PHOTOMETER_GET_WAVELENGTHS
    },
};

uint16_t HostRecipeCommandRegistry_Count(void)
{
    return (uint16_t)(sizeof(host_recipe_command_table) /
                      sizeof(host_recipe_command_table[0]));
}

const HostRecipeCommandDescriptor_t* HostRecipeCommandRegistry_Find(uint16_t command_code)
{
    for (uint16_t i = 0; i < HostRecipeCommandRegistry_Count(); i++) {
        if (host_recipe_command_table[i].command_code == command_code) {
            return &host_recipe_command_table[i];
        }
    }

    return NULL;
}
