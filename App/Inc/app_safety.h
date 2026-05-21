/*
 * app_safety.h
 *
 *  Fatal safety hook for Conductor startup/fault paths.
 */

#ifndef INC_APP_SAFETY_H_
#define INC_APP_SAFETY_H_

typedef enum {
	APP_FATAL_REASON_NONE = 0,
	APP_FATAL_REASON_GENERIC,
	APP_FATAL_REASON_STARTUP_RTOS_OBJECTS,
	APP_FATAL_REASON_STARTUP_TASK_HANDLES
} AppSafetyFatalReason_t;

void AppSafety_SetFatalReason(AppSafetyFatalReason_t reason);
AppSafetyFatalReason_t AppSafety_GetFatalReason(void);
void AppSafety_RaiseFatal(AppSafetyFatalReason_t reason);
void AppSafety_OnFatalError(void);

#endif /* INC_APP_SAFETY_H_ */
