#ifndef INC_DISPATCHER_HOST_DIRECT_OPERATION_H_
#define INC_DISPATCHER_HOST_DIRECT_OPERATION_H_

#include <stdbool.h>
#include <stdint.h>

void HostDirectOperation_Init(void);
void HostDirectOperation_StartThermoGetTemp(uint16_t host_command_code,
                                            const uint8_t* params,
                                            uint16_t params_len);
void HostDirectOperation_StartSensorGetAllTemps(uint16_t host_command_code,
                                                const uint8_t* params,
                                                uint16_t params_len);
void HostDirectOperation_StartSensorGetTemp(uint16_t host_command_code,
                                            const uint8_t* params,
                                            uint16_t params_len);
void HostDirectOperation_Run(void);
bool HostDirectOperation_IsActive(void);

#endif /* INC_DISPATCHER_HOST_DIRECT_OPERATION_H_ */
