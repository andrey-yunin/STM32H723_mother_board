#ifndef INC_DISPATCHER_HOST_RECIPE_COMMAND_REGISTRY_H_
#define INC_DISPATCHER_HOST_RECIPE_COMMAND_REGISTRY_H_

#include <stddef.h>
#include <stdint.h>
#include "recipe_types.h"

typedef struct {
    uint16_t command_code;
    uint16_t min_params_len;
    uint16_t max_params_len;
    RecipeID_t recipe_id;
} HostRecipeCommandDescriptor_t;

const HostRecipeCommandDescriptor_t* HostRecipeCommandRegistry_Find(uint16_t command_code);
uint16_t HostRecipeCommandRegistry_Count(void);

#endif /* INC_DISPATCHER_HOST_RECIPE_COMMAND_REGISTRY_H_ */
