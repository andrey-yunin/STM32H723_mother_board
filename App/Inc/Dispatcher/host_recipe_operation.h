#ifndef INC_DISPATCHER_HOST_RECIPE_OPERATION_H_
#define INC_DISPATCHER_HOST_RECIPE_OPERATION_H_

#include <stdint.h>
#include "host_command_model.h"

void HostRecipeOperation_Init(void);
uint32_t HostRecipeOperation_Start(const UniversalCommand_t* parsed_cmd);

#endif /* INC_DISPATCHER_HOST_RECIPE_OPERATION_H_ */
