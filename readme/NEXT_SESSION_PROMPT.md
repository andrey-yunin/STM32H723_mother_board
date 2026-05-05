# Next Session Prompt: Motion Board Testing

Рабочая директория:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/STM32H723_mother_board
```

Продолжаем проект `STM32H723_mother_board`. Текущая контрольная точка зафиксирована в:

- `readme/Report_20260409_Integration.md`, раздел 12: `Ревизия 2.0 (22 апреля 2026 г.): CANable E2E стенд без реальных исполнителей`
- `readme/Implementation_Plan_20260409_Integration.md`, этапы 5-6

Что уже подтверждено:

- Дирижер STM32H723 работает с CANable/SocketCAN на реальной CAN-шине.
- Неверные CAN-пины на МК исправлены; после этого FDCAN TX/RX заработал.
- `test_main_processes.py` полностью PASS через цепочку:

```text
Host USB -> Conductor -> CAN fake executors -> Conductor -> Host
```

- `App_user/can_test.py` имеет режим:

```bash
python3 App_user/can_test.py --can-only-responder -c can0
```

Он эмулирует Motion `0x20` и Fluidics `0x30`, отвечает `ACK/DONE`.

Проверенные high-level Host-команды:

```text
INIT                  0x1002
GET_STATUS            0x1000
SAMPLE_ROTATE         0x5110
DISPENSER_ASPIRATE    0x2100
DISPENSER_DISPENSE    0x2200
REAGENT_ROTATE        0x5000
MIXER_MIX             0x3100
PHOTOMETER_SCAN_SINGLE 0x6100
DISPENSER_WASH        0x2000
WASH_STATION_FILL     0x4100
WASH_STATION_WASH     0x4000
```

Подтвержденный CAN routing:

```text
Motion   0x20: HOME/ROTATE/START_CONTINUOUS/STOP, channels 0..7
Fluidics 0x30: PUMP_START/PUMP_STOP, channels 10/11
```

`GET_STATUS` после INIT:

```text
state=0x02
last_error=0x0000
```

Открытые ограничения:

- Реальные исполнители еще не проверялись.
- DATA/Thermo/Warm Finger и Big-Endian DATA path не закрыты.
- `test_main_processes.py` пока предупреждает из-за старого ожидания логов `ID:...`; нужно обновить ожидания на новый формат `Phys:<node>:<channel>`.

Следующая задача:

Переключаемся на тестирование реальной платы шаговых двигателей Motion. Нужно читать стандарты:

- `readme/DDS-240_eko_system/CONDUCTOR_INTEGRATION_GUIDE.md`, раздел Motion
- `readme/DDS-240_eko_system/DDS-240_ECOSYSTEM_STANDARD.md`
- `readme/DDS-240_eko_system/dds240_global_config.h`
- `readme/DDS-240_eko_system/can_protocol_step_motors.h`

Цель следующей работы:

1. Проверить/довести Motion-плату до стандарта DDS-240.
2. Прогнать прямые SocketCAN-тесты Motion: `GET_DEVICE_INFO`, `GET_STATUS`, `HOME`, `ROTATE`, `START_CONTINUOUS`, `STOP`, negative/NACK tests.
3. Подключить реальную Motion `0x20` к Дирижеру вместо fake Motion responder.
4. Оставить fake Fluidics `0x30` при необходимости через `can_test.py --can-only-responder`, либо добавить режим selective fake nodes.
5. После успешного Motion-теста обновить отчеты и план.

