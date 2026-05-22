#ifndef INC_DISPATCHER_SAFETY_OPERATION_H_
#define INC_DISPATCHER_SAFETY_OPERATION_H_

#include <stdbool.h>
#include <stdint.h>

void SafetyOperation_Init(void);
void SafetyOperation_Start(uint16_t host_command_code);
void SafetyOperation_Run(void);
void SafetyOperation_ClearLatch(void);
bool SafetyOperation_IsLatched(void);
bool SafetyOperation_IsLatchedOrActive(void);

#endif /* INC_DISPATCHER_SAFETY_OPERATION_H_ */
