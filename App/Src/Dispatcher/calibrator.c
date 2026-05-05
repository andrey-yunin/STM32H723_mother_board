/*
 * calibrator.c
 *
 *  Created on: May 5, 2026
 *      Author: andrey
 */

#include "Dispatcher/calibrator.h"

#define CAL_WASH_FILL_MS_PER_UL      10UL
#define CAL_PUMP_MIN_DURATION_MS     1UL
#define CAL_PUMP_MAX_DURATION_MS     300000UL

bool Calibrator_PumpVolumeToDurationMs(uint8_t pump_sys_id,
		uint16_t volume_ul,
		CalPumpOperation_t operation,
		uint32_t *duration_ms)

{
	if (duration_ms == 0 || volume_ul == 0) {
		return false;
		}
	(void)pump_sys_id;

	uint32_t ms_per_ul;

	switch (operation) {
  		case CAL_PUMP_OP_FILL:
  			ms_per_ul = CAL_WASH_FILL_MS_PER_UL;
  			break;

  		default:
  			return false;
  			}

	uint32_t result = (uint32_t)volume_ul * ms_per_ul;
	if (result < CAL_PUMP_MIN_DURATION_MS || result > CAL_PUMP_MAX_DURATION_MS) {
		return false;
		}

	*duration_ms = result;
	return true;
}



