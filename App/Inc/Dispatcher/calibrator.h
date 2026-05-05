/*
 * calibrator.h
 *
 *  Created on: May 5, 2026
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_CALIBRATOR_H_
#define INC_DISPATCHER_CALIBRATOR_H_


#include <stdint.h>
#include <stdbool.h>

typedef enum {
	CAL_PUMP_OP_FILL = 0,
  	CAL_PUMP_OP_DRAIN,
  	CAL_PUMP_OP_WASH
} CalPumpOperation_t;

bool Calibrator_PumpVolumeToDurationMs(uint8_t pump_sys_id,
		uint16_t volume_ul,
		CalPumpOperation_t operation,
		uint32_t *duration_ms);




#endif /* INC_DISPATCHER_CALIBRATOR_H_ */
