/*
 * recipe_types.h
 *
 * Recipe-domain identifiers shared by Host recipe registry, recipe store,
 * JobManager and recipe action builder.
 *
 * Owner boundary:
 * - this header defines recipe IDs only;
 * - it does not know Host packet format, parser state, CAN, RTOS or executors;
 * - command_parser maps Host command codes to these IDs through registries.
 */

#ifndef INC_DISPATCHER_RECIPE_TYPES_H_
#define INC_DISPATCHER_RECIPE_TYPES_H_

typedef enum {
	RECIPE_NONE = 0,
	RECIPE_ASPIRATE,
	RECIPE_INITIALIZE_SYSTEM,
	RECIPE_DISPENSER_WASH,
	RECIPE_WASH_STATION_WASH,
	RECIPE_SAMPLE_ROTATE,
	RECIPE_DISPENSER_ASPIRATE,
	RECIPE_DISPENSER_DISPENSE,
	RECIPE_REAGENT_ROTATE,
	RECIPE_MIXER_MIX,
	RECIPE_MIXER_WASH,
	RECIPE_MIXER_HOME,
	RECIPE_PHOTOMETER_SCAN_SINGLE,
	RECIPE_WASH_STATION_FILL,
	RECIPE_PHOTOMETER_SCAN_ALL,
	RECIPE_PHOTOMETER_CALIBRATE,
	RECIPE_PHOTOMETER_GET_WAVELENGTHS,

	/* Add new recipe IDs above this marker. */
	RECIPE_MAX_ID
} RecipeID_t;

#endif /* INC_DISPATCHER_RECIPE_TYPES_H_ */
