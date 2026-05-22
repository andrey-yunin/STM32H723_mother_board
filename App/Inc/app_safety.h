/*
 * app_safety.h
 *
 *  Fatal safety hook for Conductor startup/fault paths.
 */

#ifndef INC_APP_SAFETY_H_
#define INC_APP_SAFETY_H_

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t AppSafetyResetCauseFlags_t;

#define APP_RESET_CAUSE_NONE        0x00000000UL
#define APP_RESET_CAUSE_PIN         0x00000001UL
#define APP_RESET_CAUSE_POR         0x00000002UL
#define APP_RESET_CAUSE_BOR         0x00000004UL
#define APP_RESET_CAUSE_SOFTWARE    0x00000008UL
#define APP_RESET_CAUSE_IWDG1       0x00000010UL
#define APP_RESET_CAUSE_WWDG1       0x00000020UL
#define APP_RESET_CAUSE_LPWR1       0x00000040UL
#define APP_RESET_CAUSE_LPWR2       0x00000080UL

typedef enum {
	APP_FATAL_REASON_NONE = 0,
	APP_FATAL_REASON_GENERIC,
	APP_FATAL_REASON_STARTUP_RTOS_OBJECTS,
	APP_FATAL_REASON_STARTUP_TASK_HANDLES,
	APP_FATAL_REASON_NMI,
	APP_FATAL_REASON_HARDFAULT,
	APP_FATAL_REASON_MEMMANAGE,
	APP_FATAL_REASON_BUSFAULT,
	APP_FATAL_REASON_USAGEFAULT
} AppSafetyFatalReason_t;

void AppSafety_CaptureResetCause(void);
AppSafetyResetCauseFlags_t AppSafety_GetResetCauseFlags(void);
bool AppSafety_WasResetByIwdg(void);
void AppSafety_SetFatalReason(AppSafetyFatalReason_t reason);
AppSafetyFatalReason_t AppSafety_GetFatalReason(void);
void AppSafety_RaiseFatal(AppSafetyFatalReason_t reason);
void AppSafety_OnFatalError(void);

#endif /* INC_APP_SAFETY_H_ */
