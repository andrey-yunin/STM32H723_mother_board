# CAN Protocol: 5. Low-Level Command Set

---

This document lists the low-level commands that are sent by the Conductor to the various Executor nodes over the CAN bus. These are the primitive hardware-level operations.

## Command Groups

*   **0x01xx**: Motor Commands
*   **0x02xx**: Pump Commands
*   **0x03xx**: Sensor Commands
*   ... and so on

---

### 0x0101: MOTOR_ROTATE

Rotates a specific motor by a given number of steps.

*   **Executor Target**: **Stepper Motor Control Board** (Address `0x20`)
*   **Payload Parameters (DLC = 8)**:

| Byte(s) | Parameter  | Type    | Description                                      |
|---------|------------|---------|--------------------------------------------------|
| `0-1`   | Command Code | `UINT16`| `0x0101` (MOTOR_ROTATE)                          |
| `2`     | `motor_id` | `UINT8` | The ID of the motor on the target Executor board. |
| `3-6`   | `steps`    | `INT32` | Number of steps to rotate (can be negative). Stored as Little-Endian. |
| `7`     | `speed`    | `UINT8` | Speed profile/setting for the rotation (e.g., 0-255). |

---

### 0x0201: PUMP_RUN_DURATION

Activates a pump for a specific duration.

*   **Executor Target**: **Pump and Valve Control Board** (Address `0x30`)
*   **Payload Parameters (DLC = 7)**:

| Byte(s) | Parameter     | Type     | Description                                      |
|---------|---------------|----------|--------------------------------------------------|
| `0-1`   | Command Code  | `UINT16` | `0x0201` (PUMP_RUN_DURATION)                     |
| `2`     | `pump_id`     | `UINT8`  | The ID of the pump on the target Executor board.  |
| `3-6`   | `duration_ms` | `UINT32` | Duration to run the pump, in milliseconds. Stored as Little-Endian. |
| `7`     | `reserved`    | `UINT8`  | Must be 0. (Padding for DLC consistency, or future use).       |
