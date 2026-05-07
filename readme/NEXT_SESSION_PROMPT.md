# Next Session Prompt: Thermo Executor Refactoring

Рабочие директории:

```text
Дирижер:
/home/andrey/STM32CubeIDE/workspace_1.19.0/STM32H723_mother_board

Thermo Executor:
/home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_temp_sensors
```

## Контрольная точка

Текущий блок рефакторинга Дирижера закрыт.

Подтверждено:

- Motion Executor и Fluidics Executor считаются приведенными к общей экосистеме DDS-240.
- Дирижер приведен к текущему контракту Motion/Fluidics.
- Проект Дирижера собирался инженером после внесенных правок.
- CANable fake-executor regression PASS:

```bash
python3 App_user/can_test.py --can-only-responder -c can0
python3 App_user/test_main_processes.py
```

Проверенная цепочка:

```text
Host USB -> Conductor -> CAN fake Motion/Fluidics -> Conductor -> Host
```

Ключевые подтвержденные изменения Дирижера:

- parser хранит Host payload без физического расчета;
- физика выполняется на границе `JobManager/translator/calibrator`;
- насосные действия выполняются finite-командой `PUMP_RUN_DURATION`;
- `MIXER_MIX` использует finite Fluidics ch 12 для силовой лопатки;
- реакционный диск используется как общий механизм подвода `cuvette` для wash/photometer scenarios;
- `ParamSource_t` нормализован в сторону смысловых источников устройства.

## Документы текущего состояния

Читать перед стартом:

- `readme/Report_20260505_Conductor_Refactoring.md`
- `readme/Implementation_Plan_20260505_Conductor_Refactoring.md`
- `readme/DDS-240_eko_system/DDS-240_ECOSYSTEM_STANDARD.md`
- `readme/DDS-240_eko_system/CONDUCTOR_INTEGRATION_GUIDE.md`
- `readme/DDS-240_eko_system/Technical_Assignment_20260507_Sensor_Position_Executor.md`

Новая документация по Sensor Executor уже создана, но код Sensor Executor пока не реализован.

## Следующая задача

Переходим к аудиту и рефакторингу Thermo Executor:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_temp_sensors
```

Цель:

1. Сравнить Thermo Executor с общей экосистемой DDS-240.
2. Проверить RTOS-архитектуру: `task_can_handler`, `task_dispatcher`, `task_temp_monitor`, watchdog baseline.
3. Проверить CAN transport: 29-bit Extended ID, strict `DLC=8`, ACK/DATA/DONE ordering.
4. Проверить service-команды `0xF001..0xF007`.
5. Проверить `GET_STATUS` и диагностические метрики.
6. Проверить `app_flash` и mapping DS18B20.
7. Определить, что уже соответствует стандарту, а что требует блочного рефакторинга.

## Важные ограничения

- Не менять код Thermo до первичного аудита и согласования плана.
- Не переносить механически архитектуру Motion/Fluidics, если доменная специфика Thermo требует отличий.
- Общий каркас должен быть единым, но доменная логика DS18B20 остается специфичной.
- Отчеты вести кратко: рабочий журнал, ключевые решения, результаты сборки/тестов.

## Начальный вопрос для следующей сессии

Начать с чтения структуры проекта Thermo и сравнения с:

- `DDS-240_ECOSYSTEM_STANDARD.md`;
- разделом Thermo в `CONDUCTOR_INTEGRATION_GUIDE.md`;
- текущим `dds240_global_config.h`.
