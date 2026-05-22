/*
 * app_safety.c
 *
 *  Fatal safety hook for Conductor startup/fault paths.
 */

#include "app_safety.h"
#include "main.h"

static volatile AppSafetyFatalReason_t g_fatal_reason = APP_FATAL_REASON_NONE;
static volatile unsigned int g_fatal_latched = 0U;
static volatile AppSafetyResetCauseFlags_t g_reset_cause_flags = APP_RESET_CAUSE_NONE;
static volatile unsigned int g_reset_cause_captured = 0U;

void AppSafety_CaptureResetCause(void)
{
	AppSafetyResetCauseFlags_t flags = APP_RESET_CAUSE_NONE;

	if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != 0U) {
		flags |= APP_RESET_CAUSE_PIN;
	}
	if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != 0U) {
		flags |= APP_RESET_CAUSE_POR;
	}
	if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) != 0U) {
		flags |= APP_RESET_CAUSE_BOR;
	}
	if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != 0U) {
		flags |= APP_RESET_CAUSE_SOFTWARE;
	}
	if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDG1RST) != 0U) {
		flags |= APP_RESET_CAUSE_IWDG1;
	}
	if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDG1RST) != 0U) {
		flags |= APP_RESET_CAUSE_WWDG1;
	}
	if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWR1RST) != 0U) {
		flags |= APP_RESET_CAUSE_LPWR1;
	}
	if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWR2RST) != 0U) {
		flags |= APP_RESET_CAUSE_LPWR2;
	}

	g_reset_cause_flags = flags;
	g_reset_cause_captured = 1U;

	__HAL_RCC_CLEAR_RESET_FLAGS();
}

AppSafetyResetCauseFlags_t AppSafety_GetResetCauseFlags(void)
{
	return g_reset_cause_flags;
}

bool AppSafety_WasResetByIwdg(void)
{
	return (g_reset_cause_captured != 0U) &&
			((g_reset_cause_flags & APP_RESET_CAUSE_IWDG1) != 0U);
}

void AppSafety_SetFatalReason(AppSafetyFatalReason_t reason)
{
	if (g_fatal_reason == APP_FATAL_REASON_NONE) {
		g_fatal_reason = reason;
	}
}

AppSafetyFatalReason_t AppSafety_GetFatalReason(void)
{
	return g_fatal_reason;
}

void AppSafety_RaiseFatal(AppSafetyFatalReason_t reason)
{
	AppSafety_SetFatalReason(reason);
	Error_Handler();

	while (1) {
	}
}

void AppSafety_OnFatalError(void)
{
	if (g_fatal_reason == APP_FATAL_REASON_NONE) {
		g_fatal_reason = APP_FATAL_REASON_GENERIC;
	}

	g_fatal_latched = 1U;

	/*
	 * Future visual fault indication point.
	 *
	 * This hook is intentionally limited to deterministic actions that are
	 * safe before or during fatal halt. Later, when the board-level LED scheme
	 * is approved, this place can latch a visible fault state or encode
	 * g_fatal_reason using several LEDs.
	 *
	 * Do not use RTOS, queues, USB, CAN, logger, malloc or printf here.
	 * If blinking patterns are required, they must be implemented inside the
	 * Error_Handler() infinite loop without HAL_Delay(), because interrupts can
	 * be disabled in fatal halt.
	 */
}
