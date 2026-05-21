#ifndef INC_DISPATCHER_HOST_DIRECT_COMMAND_REGISTRY_H_
#define INC_DISPATCHER_HOST_DIRECT_COMMAND_REGISTRY_H_

#include <stddef.h>
#include <stdint.h>

typedef void (*HostDirectCommandHandler_t)(uint16_t command_code,
                                           const uint8_t* params,
                                           uint16_t params_len);

#define HOST_CMD_GET_STATUS             0x1000U
#define HOST_CMD_GET_VERSION            0x1003U
#define HOST_CMD_GET_DATETIME           0x1005U
#define HOST_CMD_EMERGENCY_STOP         0x1010U
#define HOST_CMD_THERMO_GET_TEMP        0x8000U
#define HOST_CMD_SENSOR_GET_ALL_TEMPS   0x9010U
#define HOST_CMD_SENSOR_GET_TEMP        0x9011U

typedef struct {
    uint16_t command_code;
    uint16_t min_params_len;
    uint16_t max_params_len;
    HostDirectCommandHandler_t handler;
} HostDirectCommandDescriptor_t;

const HostDirectCommandDescriptor_t* HostDirectCommandRegistry_Find(uint16_t command_code);
uint16_t HostDirectCommandRegistry_Count(void);

#endif /* INC_DISPATCHER_HOST_DIRECT_COMMAND_REGISTRY_H_ */
