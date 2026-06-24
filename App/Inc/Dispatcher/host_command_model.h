/*
 * host_command_model.h
 *
 * Structured model of a parsed Host command.
 *
 * Owner boundary:
 * - filled by command_parser/parameter_parser after Host packet validation;
 * - consumed by host_recipe_operation, JobManager and recipe action builder;
 * - contains Host API arguments and selected RecipeID_t;
 * - does not perform parsing, send Host responses, use CAN or own RTOS objects.
 */

#ifndef INC_DISPATCHER_HOST_COMMAND_MODEL_H_
#define INC_DISPATCHER_HOST_COMMAND_MODEL_H_

#include <stdint.h>
#include "app_config.h"
#include "recipe_types.h"

typedef struct {
	uint8_t modules_mask;
} ParsedArgs_Init;

typedef struct {
	uint8_t dispenser_id;
	int32_t syringe_steps;
	int32_t rotate_steps;
	int32_t steps_down;
	int32_t steps_up;
	uint16_t cycles;
} ParsedArgs_DispenserWash;

typedef struct {
	uint8_t cycles;
	uint16_t cuvette;
} ParsedArgs_WashStationWash;

typedef struct {
	uint16_t slot;
} ParsedArgs_SampleRotate;

typedef struct {
	uint8_t dispenser_id;
	uint8_t source_type;
	int32_t rotate_steps;
	int32_t steps_down;
	int32_t steps_up;
	int32_t syringe_steps;
} ParsedArgs_DispenserAspirate;

typedef struct {
	uint8_t dispenser_id;
	uint8_t target_type;
	int32_t rotate_steps;
	int32_t steps_down;
	int32_t steps_up;
	int32_t syringe_steps;
} ParsedArgs_DispenserDispense;

typedef struct {
	uint8_t rotor_id;
	uint16_t slot;
} ParsedArgs_ReagentRotate;

typedef struct {
	uint8_t mixer_id;
	uint8_t cycles;
} ParsedArgs_MixerWash;

typedef struct {
	uint8_t mixer_id;
	uint16_t cuvette;
	uint16_t duration_ms;
	uint8_t wash_cycles;
} ParsedArgs_MixerMix;

typedef struct {
	uint8_t mixer_id;
} ParsedArgs_MixerHome;

typedef struct {
	uint16_t cuvette;
	uint8_t wavelength_mask;
} ParsedArgs_PhotometerScanSingle;

typedef struct {
	uint8_t wavelength_mask;
} ParsedArgs_PhotometerScanAll;

typedef struct {
	uint8_t calibration_type;
	uint8_t wavelength_mask;
} ParsedArgs_PhotometerCalibrate;


typedef struct {
	uint16_t volume_ul;
	uint16_t cuvette;
} ParsedArgs_WashStationFill;

typedef struct {
	uint8_t sensor_id;
} ParsedArgs_SensorGetTemp;

typedef struct {
	uint8_t raw[MAX_BINARY_ARGS_SIZE];
	uint16_t len;
} BinaryArgs_t;

typedef enum {
	ARGS_TYPE_NONE = 0,
	ARGS_TYPE_STRING,
	ARGS_TYPE_BINARY,
	ARGS_TYPE_PARSED
} HostCommandArgsType_t;

typedef struct {
	uint16_t command_code;
	RecipeID_t recipe_id;
	HostCommandArgsType_t args_type;

	union {
		char string[APP_USB_CMD_MAX_LEN];
		BinaryArgs_t binary;

		ParsedArgs_Init init;
		ParsedArgs_DispenserWash dispenser_wash;
		ParsedArgs_WashStationWash wash_station_wash;
		ParsedArgs_SampleRotate sample_rotate;
		ParsedArgs_DispenserAspirate dispenser_aspirate;
		ParsedArgs_DispenserDispense dispenser_dispense;
		ParsedArgs_ReagentRotate reagent_rotate;
		ParsedArgs_MixerMix mixer_mix;
		ParsedArgs_MixerWash mixer_wash;
		ParsedArgs_MixerHome mixer_home;
		ParsedArgs_PhotometerScanSingle photometer_scan_single;
		ParsedArgs_WashStationFill wash_station_fill;
		ParsedArgs_SensorGetTemp sensor_get_temp;
		ParsedArgs_PhotometerScanAll photometer_scan_all;
		ParsedArgs_PhotometerCalibrate photometer_calibrate;

	} args;
} UniversalCommand_t;

#endif /* INC_DISPATCHER_HOST_COMMAND_MODEL_H_ */
