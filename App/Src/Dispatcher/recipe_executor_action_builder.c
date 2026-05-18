/*
 * recipe_executor_action_builder.c
 *
 * Builds low-level executor commands from recipe atomic actions.
 */

#include "recipe_executor_action_builder.h"
#include "calibrator.h"
#include "device_mapping.h"
#include "param_translator.h"
#include "system_mapping.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>

static uint32_t RecipeExecutorActionBuilder_MaxU32(uint32_t a, uint32_t b)
{
    return (a > b) ? a : b;
}

static uint32_t RecipeExecutorActionBuilder_AbsSteps(int32_t steps)
{
    return (steps < 0) ? ((uint32_t)(-(steps + 1)) + 1U) : (uint32_t)steps;
}

static uint32_t RecipeExecutorActionBuilder_CalcMotionRotateTimeoutMs(
        int32_t steps,
        uint16_t speed)
{
    uint32_t abs_steps = RecipeExecutorActionBuilder_AbsSteps(steps);

    if (abs_steps == 0U) {
        return JOB_TIMEOUT_MS;
    }

    uint64_t motion_ms = (((uint64_t)abs_steps * 1000U) + speed - 1U) / speed;

    if (motion_ms > (UINT32_MAX - JOB_MOTION_ROTATE_MARGIN_MS)) {
        return UINT32_MAX;
    }
    return (uint32_t)motion_ms + JOB_MOTION_ROTATE_MARGIN_MS;
}

static void RecipeExecutorActionBuilder_SetError(
        char* error_msg,
        size_t error_msg_len,
        const char* format,
        uint32_t job_id,
        uint32_t value)
{
    if (error_msg == NULL || error_msg_len == 0U) {
        return;
    }

    snprintf(error_msg, error_msg_len, format,
            (unsigned long)job_id,
            (unsigned int)value);
}

static uint8_t RecipeExecutorActionBuilder_ResolveUint8Param(
        const RecipeExecutorActionContext_t* ctx,
        ParamSource_t source,
        uint8_t static_value)
{
    if (ctx != NULL &&
            ctx->initial_cmd != NULL &&
            ctx->initial_cmd->args_type == ARGS_TYPE_PARSED) {
        switch (source) {
            case PARAM_SOURCE_CMD_INIT_MASK:
                return ctx->initial_cmd->args.init.modules_mask;

            case PARAM_SOURCE_DISPENSER_ID:
                switch (ctx->recipe_id) {
                    case RECIPE_DISPENSER_WASH:
                        return ctx->initial_cmd->args.dispenser_wash.dispenser_id;
                    case RECIPE_DISPENSER_ASPIRATE:
                        return ctx->initial_cmd->args.dispenser_aspirate.dispenser_id;
                    case RECIPE_DISPENSER_DISPENSE:
                        return ctx->initial_cmd->args.dispenser_dispense.dispenser_id;
                    default:
                        break;
                }
                break;

            case PARAM_SOURCE_WASH_STATION_CYCLES:
                return ctx->initial_cmd->args.wash_station_wash.cycles;

            case PARAM_SOURCE_MIXER_ID:
                return ctx->initial_cmd->args.mixer_mix.mixer_id;

            case PARAM_SOURCE_PHOTOMETER_WAVELENGTH_MASK:
                return ctx->initial_cmd->args.photometer_scan_single.wavelength_mask;

            default:
                break;
        }
    }

    return static_value;
}

static uint16_t RecipeExecutorActionBuilder_ResolveUint16Param(
        const RecipeExecutorActionContext_t* ctx,
        ParamSource_t source,
        uint16_t static_value)
{
    if (ctx != NULL &&
            ctx->initial_cmd != NULL &&
            ctx->initial_cmd->args_type == ARGS_TYPE_PARSED) {
        switch (source) {
            case PARAM_SOURCE_DISPENSER_CYCLES:
                return ctx->initial_cmd->args.dispenser_wash.cycles;

            default:
                break;
        }
    }

    return static_value;
}

static int32_t RecipeExecutorActionBuilder_ResolveInt32Param(
        const RecipeExecutorActionContext_t* ctx,
        ParamSource_t source,
        int32_t static_value)
{
    if (ctx != NULL &&
            ctx->initial_cmd != NULL &&
            ctx->initial_cmd->args_type == ARGS_TYPE_PARSED) {
        switch (source) {
            case PARAM_SOURCE_REACTION_DISK_ROTATE_STEPS:
                switch (ctx->recipe_id) {
                    case RECIPE_WASH_STATION_WASH:
                        return ParamTranslator_CuvetteToSteps(
                                ctx->initial_cmd->args.wash_station_wash.cuvette);
                    case RECIPE_WASH_STATION_FILL:
                        return ParamTranslator_CuvetteToSteps(
                                ctx->initial_cmd->args.wash_station_fill.cuvette);
                    case RECIPE_PHOTOMETER_SCAN_SINGLE:
                        return ParamTranslator_CuvetteToSteps(
                                ctx->initial_cmd->args.photometer_scan_single.cuvette);
                    default:
                        break;
                }
                break;

            case PARAM_SOURCE_REAGENT_SAMPLE_ROTATE_STEPS:
                switch (ctx->recipe_id) {
                    case RECIPE_SAMPLE_ROTATE:
                        return ParamTranslator_SampleDiskSlotToSteps(
                                ctx->initial_cmd->args.sample_rotate.slot);
                    case RECIPE_REAGENT_ROTATE:
                        return ParamTranslator_ReagentRotorSlotToSteps(
                                ctx->initial_cmd->args.reagent_rotate.rotor_id,
                                ctx->initial_cmd->args.reagent_rotate.slot);
                    default:
                        break;
                }
                break;

            case PARAM_SOURCE_DISPENSER_ROTATE_STEPS:
                switch (ctx->recipe_id) {
                    case RECIPE_DISPENSER_WASH:
                        return ctx->initial_cmd->args.dispenser_wash.rotate_steps;
                    case RECIPE_DISPENSER_ASPIRATE:
                        return ctx->initial_cmd->args.dispenser_aspirate.rotate_steps;
                    case RECIPE_DISPENSER_DISPENSE:
                        return ctx->initial_cmd->args.dispenser_dispense.rotate_steps;
                    default:
                        break;
                }
                break;

            case PARAM_SOURCE_DISPENSER_Z_STEPS_DOWN:
                switch (ctx->recipe_id) {
                    case RECIPE_DISPENSER_WASH:
                        return ctx->initial_cmd->args.dispenser_wash.steps_down;
                    case RECIPE_DISPENSER_ASPIRATE:
                        return ctx->initial_cmd->args.dispenser_aspirate.steps_down;
                    case RECIPE_DISPENSER_DISPENSE:
                        return ctx->initial_cmd->args.dispenser_dispense.steps_down;
                    default:
                        break;
                }
                break;

            case PARAM_SOURCE_DISPENSER_Z_STEPS_UP:
                switch (ctx->recipe_id) {
                    case RECIPE_DISPENSER_WASH:
                        return ctx->initial_cmd->args.dispenser_wash.steps_up;
                    case RECIPE_DISPENSER_ASPIRATE:
                        return ctx->initial_cmd->args.dispenser_aspirate.steps_up;
                    case RECIPE_DISPENSER_DISPENSE:
                        return ctx->initial_cmd->args.dispenser_dispense.steps_up;
                    default:
                        break;
                }
                break;

            case PARAM_SOURCE_MIXER_XY_STEPS:
                return ParamTranslator_MixerCuvetteToXYSteps(
                        ctx->initial_cmd->args.mixer_mix.cuvette);

            case PARAM_SOURCE_MIXER_Z_STEPS_DOWN:
                return ParamTranslator_MixerZToStepsDown(
                        ctx->initial_cmd->args.mixer_mix.mixer_id,
                        ctx->initial_cmd->args.mixer_mix.cuvette);

            case PARAM_SOURCE_MIXER_Z_STEPS_UP:
                return ParamTranslator_MixerZToStepsUp(
                        ctx->initial_cmd->args.mixer_mix.mixer_id,
                        ctx->initial_cmd->args.mixer_mix.cuvette);

            case PARAM_SOURCE_DISPENSER_SYRINGE_STEPS:
                switch (ctx->recipe_id) {
                    case RECIPE_DISPENSER_WASH:
                        return ctx->initial_cmd->args.dispenser_wash.syringe_steps;
                    case RECIPE_DISPENSER_ASPIRATE:
                        return ctx->initial_cmd->args.dispenser_aspirate.syringe_steps;
                    case RECIPE_DISPENSER_DISPENSE:
                        return ctx->initial_cmd->args.dispenser_dispense.syringe_steps;
                    default:
                        break;
                }
                break;

            default:
                break;
        }
    }

    return static_value;
}

static uint32_t RecipeExecutorActionBuilder_ResolveUint32Param(
        const RecipeExecutorActionContext_t* ctx,
        ParamSource_t source,
        uint32_t static_value)
{
    if (ctx != NULL &&
            ctx->initial_cmd != NULL &&
            ctx->initial_cmd->args_type == ARGS_TYPE_PARSED) {
        switch (source) {
            case PARAM_SOURCE_WASH_STATION_FILL_DURATION_MS: {
                uint32_t duration_ms = 0U;
                if (!Calibrator_PumpVolumeToDurationMs(
                        SYS_WASH_PUMP_FILL,
                        ctx->initial_cmd->args.wash_station_fill.volume_ul,
                        CAL_PUMP_OP_FILL,
                        &duration_ms)) {
                    return 0U;
                }
                return duration_ms;
            }

            case PARAM_SOURCE_MIXER_PADDLE_DURATION_MS:
                return ctx->initial_cmd->args.mixer_mix.duration_ms;

            default:
                break;
        }
    }

    return static_value;
}

static bool RecipeExecutorActionBuilder_BuildPumpDuration(
        const RecipeExecutorActionContext_t* ctx,
        const AtomicAction_t* atomic_action,
        RecipeExecutorAction_t* out_action,
        char* error_msg,
        size_t error_msg_len)
{
    uint8_t sys_id = RecipeExecutorActionBuilder_ResolveUint8Param(
            ctx,
            atomic_action->params.pump_duration.pump_id_source,
            atomic_action->params.pump_duration.pump_id);
    uint32_t duration_ms = RecipeExecutorActionBuilder_ResolveUint32Param(
            ctx,
            atomic_action->params.pump_duration.duration_ms_source,
            atomic_action->params.pump_duration.duration_ms);

    if (duration_ms == 0U) {
        RecipeExecutorActionBuilder_SetError(
                error_msg,
                error_msg_len,
                "ERROR: Job #%lu: Invalid pump duration for SysID %u",
                ctx->job_id,
                sys_id);
        return false;
    }

    DevicePhysAddr_t phys_addr = DeviceMapping_GetFluidicPhysAddr(sys_id);
    if (!phys_addr.is_valid) {
        RecipeExecutorActionBuilder_SetError(
                error_msg,
                error_msg_len,
                "ERROR: Job #%lu: Invalid Pump SysID %u",
                ctx->job_id,
                sys_id);
        return false;
    }

    memset(out_action, 0, sizeof(*out_action));
    out_action->command_required = true;
    out_action->low_command_code = CAN_CMD_PUMP_RUN_DURATION;
    out_action->node_id = phys_addr.node_id;
    out_action->channel = phys_addr.ch_idx;
    out_action->channel_valid = true;
    out_action->response_policy = EXECUTOR_ACTION_RESPONSE_DONE_ONLY;
    out_action->operation_timeout_ms = RecipeExecutorActionBuilder_MaxU32(
            ctx->current_step_timeout_ms,
            duration_ms + JOB_PUMP_DURATION_MARGIN_MS);
    out_action->action_label = "RUN_PUMP_DURATION";
    out_action->debug_value = duration_ms;

    Packer_CreatePumpRunDurationMsg(phys_addr.ch_idx, duration_ms, &out_action->can_msg);
    out_action->can_msg.id = CAN_BUILD_ID(
            CAN_PRIORITY_HIGH,
            CAN_MSG_TYPE_COMMAND,
            phys_addr.node_id,
            CAN_ADDR_CONDUCTOR);

    return true;
}

static bool RecipeExecutorActionBuilder_BuildPumpSwitch(
        const RecipeExecutorActionContext_t* ctx,
        const AtomicAction_t* atomic_action,
        bool start,
        RecipeExecutorAction_t* out_action,
        char* error_msg,
        size_t error_msg_len)
{
    uint8_t sys_id = RecipeExecutorActionBuilder_ResolveUint8Param(
            ctx,
            atomic_action->params.pump.pump_id_source,
            atomic_action->params.pump.pump_id);

    DevicePhysAddr_t phys_addr = DeviceMapping_GetFluidicPhysAddr(sys_id);
    if (!phys_addr.is_valid) {
        RecipeExecutorActionBuilder_SetError(
                error_msg,
                error_msg_len,
                "ERROR: Job #%lu: Invalid Pump SysID %u",
                ctx->job_id,
                sys_id);
        return false;
    }

    memset(out_action, 0, sizeof(*out_action));
    out_action->command_required = true;
    out_action->low_command_code = start ? CAN_CMD_PUMP_START : CAN_CMD_PUMP_STOP;
    out_action->node_id = phys_addr.node_id;
    out_action->channel = phys_addr.ch_idx;
    out_action->channel_valid = true;
    out_action->response_policy = EXECUTOR_ACTION_RESPONSE_DONE_ONLY;
    out_action->operation_timeout_ms = ctx->current_step_timeout_ms;
    out_action->action_label = start ? "START_PUMP" : "STOP_PUMP";

    if (start) {
        Packer_CreatePumpStartMsg(phys_addr.ch_idx, 0, &out_action->can_msg);
    } else {
        Packer_CreatePumpStopMsg(phys_addr.ch_idx, &out_action->can_msg);
    }
    out_action->can_msg.id = CAN_BUILD_ID(
            CAN_PRIORITY_HIGH,
            CAN_MSG_TYPE_COMMAND,
            phys_addr.node_id,
            CAN_ADDR_CONDUCTOR);

    return true;
}

static bool RecipeExecutorActionBuilder_BuildRotateMotor(
        const RecipeExecutorActionContext_t* ctx,
        const AtomicAction_t* atomic_action,
        RecipeExecutorAction_t* out_action,
        char* error_msg,
        size_t error_msg_len)
{
    uint8_t sys_id = RecipeExecutorActionBuilder_ResolveUint8Param(
            ctx,
            atomic_action->params.rotate_motor.motor_id_source,
            atomic_action->params.rotate_motor.motor_id);
    int32_t steps = RecipeExecutorActionBuilder_ResolveInt32Param(
            ctx,
            atomic_action->params.rotate_motor.steps_source,
            atomic_action->params.rotate_motor.steps);
    uint16_t speed = RecipeExecutorActionBuilder_ResolveUint16Param(
            ctx,
            atomic_action->params.rotate_motor.speed_source,
            atomic_action->params.rotate_motor.speed);

    uint32_t abs_steps = RecipeExecutorActionBuilder_AbsSteps(steps);
    if (abs_steps != 0U && speed == 0U) {
        RecipeExecutorActionBuilder_SetError(
                error_msg,
                error_msg_len,
                "ERROR: Job #%lu: Invalid Motion speed 0 for SysID %u",
                ctx->job_id,
                sys_id);
        return false;
    }

    DevicePhysAddr_t phys_addr = DeviceMapping_GetMotorPhysAddr(sys_id);
    if (!phys_addr.is_valid) {
        RecipeExecutorActionBuilder_SetError(
                error_msg,
                error_msg_len,
                "ERROR: Job #%lu: Invalid Motor SysID %u",
                ctx->job_id,
                sys_id);
        return false;
    }

    memset(out_action, 0, sizeof(*out_action));
    out_action->command_required = true;
    out_action->low_command_code = CAN_CMD_MOTOR_ROTATE;
    out_action->node_id = phys_addr.node_id;
    out_action->channel = phys_addr.ch_idx;
    out_action->channel_valid = true;
    out_action->response_policy = EXECUTOR_ACTION_RESPONSE_DONE_ONLY;
    out_action->operation_timeout_ms = RecipeExecutorActionBuilder_MaxU32(
            ctx->current_step_timeout_ms,
            RecipeExecutorActionBuilder_CalcMotionRotateTimeoutMs(steps, speed));
    out_action->action_label = "ROTATE_MOTOR";
    out_action->debug_value = speed;
    out_action->debug_signed_value = steps;

    Packer_CreateRotateMotorMsg(phys_addr.ch_idx, steps, speed, &out_action->can_msg);
    out_action->can_msg.id = CAN_BUILD_ID(
            CAN_PRIORITY_HIGH,
            CAN_MSG_TYPE_COMMAND,
            phys_addr.node_id,
            CAN_ADDR_CONDUCTOR);

    return true;
}

static bool RecipeExecutorActionBuilder_BuildHomeMotor(
        const RecipeExecutorActionContext_t* ctx,
        const AtomicAction_t* atomic_action,
        RecipeExecutorAction_t* out_action,
        char* error_msg,
        size_t error_msg_len)
{
    uint8_t sys_id = RecipeExecutorActionBuilder_ResolveUint8Param(
            ctx,
            atomic_action->params.home_motor.motor_id_source,
            atomic_action->params.home_motor.motor_id);
    uint16_t speed = RecipeExecutorActionBuilder_ResolveUint16Param(
            ctx,
            atomic_action->params.home_motor.speed_source,
            atomic_action->params.home_motor.speed);

    memset(out_action, 0, sizeof(*out_action));

    if (ctx->recipe_id == RECIPE_INITIALIZE_SYSTEM &&
            ctx->initial_cmd->args_type == ARGS_TYPE_PARSED) {
        uint8_t modules_mask = ctx->initial_cmd->args.init.modules_mask;
        if (sys_id == 0U || sys_id > 8U ||
                (modules_mask & (uint8_t)(1U << (sys_id - 1U))) == 0U) {
            out_action->command_required = false;
            out_action->action_label = "HOME_MOTOR";
            return true;
        }
    }

    if (speed == 0U) {
        RecipeExecutorActionBuilder_SetError(
                error_msg,
                error_msg_len,
                "ERROR: Job #%lu: Invalid HOME speed 0 for SysID %u",
                ctx->job_id,
                sys_id);
        return false;
    }

    DevicePhysAddr_t phys_addr = DeviceMapping_GetMotorPhysAddr(sys_id);
    if (!phys_addr.is_valid) {
        RecipeExecutorActionBuilder_SetError(
                error_msg,
                error_msg_len,
                "ERROR: Job #%lu: Invalid Motor SysID %u",
                ctx->job_id,
                sys_id);
        return false;
    }

    out_action->command_required = true;
    out_action->low_command_code = CAN_CMD_MOTOR_HOME;
    out_action->node_id = phys_addr.node_id;
    out_action->channel = phys_addr.ch_idx;
    out_action->channel_valid = true;
    out_action->response_policy = EXECUTOR_ACTION_RESPONSE_DONE_ONLY;
    out_action->operation_timeout_ms = RecipeExecutorActionBuilder_MaxU32(
            ctx->current_step_timeout_ms,
            JOB_MOTION_HOME_TIMEOUT_MS);
    out_action->action_label = "HOME_MOTOR";
    out_action->debug_value = speed;

    Packer_CreateHomeMotorMsg(phys_addr.ch_idx, speed, &out_action->can_msg);
    out_action->can_msg.id = CAN_BUILD_ID(
            CAN_PRIORITY_HIGH,
            CAN_MSG_TYPE_COMMAND,
            phys_addr.node_id,
            CAN_ADDR_CONDUCTOR);

    return true;
}

bool RecipeExecutorActionBuilder_Build(
        const RecipeExecutorActionContext_t* ctx,
        const AtomicAction_t* atomic_action,
        RecipeExecutorAction_t* out_action,
        char* error_msg,
        size_t error_msg_len)
{
    if (ctx == NULL || atomic_action == NULL || out_action == NULL || ctx->initial_cmd == NULL) {
        RecipeExecutorActionBuilder_SetError(
                error_msg,
                error_msg_len,
                "ERROR: Job #%lu: Invalid recipe executor action context (%u)",
                ctx != NULL ? ctx->job_id : 0U,
                0U);
        return false;
    }

    switch (atomic_action->action) {
        case ACTION_ROTATE_MOTOR:
            return RecipeExecutorActionBuilder_BuildRotateMotor(
                    ctx,
                    atomic_action,
                    out_action,
                    error_msg,
                    error_msg_len);

        case ACTION_HOME_MOTOR:
            return RecipeExecutorActionBuilder_BuildHomeMotor(
                    ctx,
                    atomic_action,
                    out_action,
                    error_msg,
                    error_msg_len);

        case ACTION_RUN_PUMP_DURATION:
            return RecipeExecutorActionBuilder_BuildPumpDuration(
                    ctx,
                    atomic_action,
                    out_action,
                    error_msg,
                    error_msg_len);

        case ACTION_START_PUMP:
            return RecipeExecutorActionBuilder_BuildPumpSwitch(
                    ctx,
                    atomic_action,
                    true,
                    out_action,
                    error_msg,
                    error_msg_len);

        case ACTION_STOP_PUMP:
            return RecipeExecutorActionBuilder_BuildPumpSwitch(
                    ctx,
                    atomic_action,
                    false,
                    out_action,
                    error_msg,
                    error_msg_len);

        default:
            RecipeExecutorActionBuilder_SetError(
                    error_msg,
                    error_msg_len,
                    "ERROR: Job #%lu: Unsupported recipe executor action %u",
                    ctx->job_id,
                    (uint32_t)atomic_action->action);
            return false;
    }
}
