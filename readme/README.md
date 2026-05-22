# DDS-240 shared documentation pointer

This project uses the shared DDS-240 documentation folder from the STM32CubeIDE
workspace instead of keeping a local copy of ecosystem documents.

## Shared documentation

Common documentation root:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme
```

Ecosystem documentation:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system
```

Conductor documentation:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Conductor
```

## Key entry points

Ecosystem standard:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/DDS-240_ECOSYSTEM_STANDARD.md
```

Global next session prompt:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/NEXT_SESSION_PROMPT.md
```

Conductor next session prompt:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Conductor/NEXT_SESSION_PROMPT.md
```

Conductor integration guide:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Conductor/CONDUCTOR_INTEGRATION_GUIDE.md
```

Conductor technical documentation:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Conductor/Technical_Documentation.md
```

Conductor refactoring report:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Conductor/Report_20260505_Conductor_Refactoring.md
```

Conductor refactoring plan:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Conductor/Implementation_Plan_20260505_Conductor_Refactoring.md
```

## Latest project checkpoint

Date: 2026-05-22.

Status:

- Conductor architecture refactoring baseline is closed through:
  - `SAFETY_OPERATION 0x1010`;
  - `EXECUTOR_BACKED_DIRECT 0x9010/0x9011`;
  - `JobManager` Host completion boundary;
  - Host model boundary.
- USER USB CDC failure was diagnosed and closed:
  - symptom: Linux saw only ST-LINK VCP, no `STM32 Virtual ComPort`;
  - cause: FreeRTOS heap `32768` became too small after new routed queues/lifecycle boundaries;
  - failure mode: startup fatal before USB task, then hardware IWDG reset loop about every 8 seconds;
  - fix: CubeMX `FREERTOS.configTOTAL_HEAP_SIZE=65536`;
  - result: `STM32 Virtual ComPort` enumerates as `/dev/ttyACM1`.
- Temporary LED diagnostic checkpoints were removed.
- Engineer confirmed clean firmware build after LED-marker removal.
- Host-only acceptance passed on the new build without startup delay or DTR/RTS workarounds:

```bash
cd /home/andrey/STM32CubeIDE/workspace_1.19.0/STM32H723_mother_board/App_user
python3 test_main_processes.py --host-only -s /dev/ttyACM1
```

Covered:

```text
GET_STATUS
GET_VERSION
GET_DATETIME
GET_STATUS bad CRC -> NACK 0x0007
GET_STATUS invalid params -> NACK 0x0003
```

## Active working rules

- Do not change Host command codes, parameters, payloads or response lifecycle without a separate ecosystem decision.
- CubeMX-owned settings are changed by the engineer through CubeMX only. Assistant provides instructions and reviews generated diffs.
- Firmware builds are run by the engineer; after code changes, request a clean build confirmation.
- Do not restore deleted local documentation copies; shared docs live under `DDS-240_readme`.
- Test scripts in `App_user` are aligned to the current Host protocol and should not rely on text logs as pass criteria.

## Next work block

Focused E2E/hardware checks:

```text
service F001/F004/F007
Thermo direct 0x9010/0x9011
NACK without DONE
ACK without DONE -> timeout/recovery
```
