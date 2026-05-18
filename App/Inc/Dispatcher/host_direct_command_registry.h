#ifndef INC_DISPATCHER_HOST_DIRECT_COMMAND_REGISTRY_H_
#define INC_DISPATCHER_HOST_DIRECT_COMMAND_REGISTRY_H_

#include <stddef.h>
#include <stdint.h>

typedef void (*HostDirectCommandHandler_t)(uint16_t command_code,
                                           const uint8_t* params,
                                           uint16_t params_len);

typedef struct {
    uint16_t command_code;
    uint16_t min_params_len;
    uint16_t max_params_len;
    HostDirectCommandHandler_t handler;
} HostDirectCommandDescriptor_t;

const HostDirectCommandDescriptor_t* HostDirectCommandRegistry_Find(uint16_t command_code);
uint16_t HostDirectCommandRegistry_Count(void);

#endif /* INC_DISPATCHER_HOST_DIRECT_COMMAND_REGISTRY_H_ */
