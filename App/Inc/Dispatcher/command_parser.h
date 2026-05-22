/*
 * command_parser.h
 *
 * Host parser API boundary.
 *
 * Owner boundary:
 * - exposes only parser entry points used by task_dispatcher/debug console;
 * - Host argument structs live in host_command_model.h;
 * - RecipeID_t lives in recipe_types.h;
 * - parser behavior and Host protocol format are implemented in command_parser.c.
 */

#ifndef INC_DISPATCHER_COMMAND_PARSER_H_
#define INC_DISPATCHER_COMMAND_PARSER_H_

#include <stdint.h>

void Parser_ProcessCommand(char *command_line);
void Parser_ProcessBinaryCommand(uint8_t *packet, uint16_t len);

#endif /* INC_DISPATCHER_COMMAND_PARSER_H_ */
