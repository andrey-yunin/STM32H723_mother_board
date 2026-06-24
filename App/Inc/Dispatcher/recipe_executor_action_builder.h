#ifndef INC_DISPATCHER_RECIPE_EXECUTOR_ACTION_BUILDER_H_
#define INC_DISPATCHER_RECIPE_EXECUTOR_ACTION_BUILDER_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "can_packer.h"
#include "host_command_model.h"
#include "recipe_store.h"

typedef enum {
    EXECUTOR_ACTION_RESPONSE_DONE_ONLY = 0,
    EXECUTOR_ACTION_RESPONSE_DATA_THEN_DONE,
    EXECUTOR_ACTION_RESPONSE_MULTI_DATA_THEN_DONE
} ExecutorActionResponsePolicy_t;

typedef struct {
    bool command_required;
    uint16_t low_command_code;
    uint8_t node_id;
    uint8_t channel;
    bool channel_valid;
    ExecutorActionResponsePolicy_t response_policy;
    uint32_t operation_timeout_ms;
    CAN_Message_t can_msg;
    const char* action_label;
    uint32_t debug_value;
    int32_t debug_signed_value;
} RecipeExecutorAction_t;

typedef struct {
    uint32_t job_id;
    RecipeID_t recipe_id;
    const UniversalCommand_t* initial_cmd;
    uint32_t current_step_timeout_ms;

    /*
     * Runtime-cursor для Host 0x6000 PHOTOMETER_SCAN_ALL.
     * Host-команда не содержит cuvette, поэтому текущую кювету задает JobManager.
     */
    bool photometer_scan_all_active;
    uint16_t photometer_scan_all_current_cuvette;
} RecipeExecutorActionContext_t;

bool RecipeExecutorActionBuilder_Build(
        const RecipeExecutorActionContext_t* ctx,
        const AtomicAction_t* atomic_action,
        RecipeExecutorAction_t* out_actions,
        uint8_t max_actions,
        uint8_t* out_count,
        char* error_msg,
        size_t error_msg_len);

#endif /* INC_DISPATCHER_RECIPE_EXECUTOR_ACTION_BUILDER_H_ */
