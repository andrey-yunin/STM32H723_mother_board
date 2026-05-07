# План рефакторинга Дирижера под общую экосистему DDS-240

**Дата:** 5 мая 2026 г.  
**Проект:** STM32H723_mother_board, Дирижер DDS-240  
**Роль Дирижера:** стабильный шлюз `Host API -> recipe/translator/mapping -> strict executor CAN`.

## 0. Маркеры исполнения

Используем единые маркеры для ведения плана:

| Маркер | Смысл |
|:---:|:---|
| `[ ]` | Не начато |
| `[~]` | В работе |
| `[x]` | Выполнено |
| `[!]` | Заблокировано или требует решения |

Текущий статус файла: блок `WASH_STATION_FILL/WASH_STATION_WASH -> PUMP_RUN_DURATION` закрыт сборкой и CANable E2E-регрессией.

## 0.1. Режим работы

- [x] Кодовые правки выполняет ведущий инженер проекта.
- [x] AI-ассистент работает как учитель-консультант: объясняет архитектуру, проверяет решения, предлагает локальные блоки изменений.
- [x] AI-ассистент может самостоятельно корректировать документацию, планы и отчеты.
- [x] Исходный код AI-ассистент не меняет без явной команды `внеси правки` или аналогичного прямого разрешения.
- [x] Командные блоки для кода должны быть короткими, с комментариями по делу: что меняем, где граница ответственности и почему.

## 1. Канонические источники

Эти документы считаются источником истины для рефакторинга:

- [x] `readme/DDS-240_eko_system/dds240_global_config.h`
- [x] `readme/DDS-240_eko_system/DDS-240_ECOSYSTEM_STANDARD.md`
- [x] `readme/DDS-240_eko_system/CONDUCTOR_INTEGRATION_GUIDE.md`
- [x] `readme/DDS-240_eko_system/FLUIDICS_PUMP_RUN_DURATION_MIGRATION.md`
- [x] `readme/DDS-240_eko_system/Commands_API/User_Commands/commands.md`

Исторические документы можно использовать как контекст, но при расхождении приоритет у `dds240_global_config.h` и ecosystem standard.

## 2. Текущий baseline

- [x] Документация `readme` просмотрена.
- [x] Ключевые модули Дирижера просмотрены: `can_packer`, `job_manager`, `recipe_store`, `parameter_parser`, `service_manager`, `device_mapping`.
- [ ] Собрать проект перед правками и зафиксировать предупреждения.
- [x] Зафиксировать текущий smoke baseline с fake executors.
- [x] Обновить отчет после первого рефакторингового патча.

Подтвержденная рабочая точка из документации:

```text
Host USB -> Conductor -> CAN fake executors -> Conductor -> Host
```

Проверенные high-level команды на CANable E2E:

```text
INIT, GET_STATUS, SAMPLE_ROTATE, DISPENSER_ASPIRATE,
DISPENSER_DISPENSE, REAGENT_ROTATE, MIXER_MIX,
PHOTOMETER_SCAN_SINGLE, DISPENSER_WASH,
WASH_STATION_FILL, WASH_STATION_WASH
```

## 3. Главные архитектурные инварианты

- [x] Дирижер и все исполнители DDS-240 работают в одной экосистеме; общие правила обязательны для всех участников.
- [x] Локальная специфика платы допустима только ниже общего контракта экосистемы.
- [x] Все настройки неспецифичного уровня должны единообразно применяться к Дирижеру, Motion, Fluidics, Thermo и будущим исполнителям.
- [ ] Host API не менять ради удобства executor payload.
- [ ] `Host -> Conductor` сохраняет documented dynamic payload.
- [ ] `Conductor <-> Executor` всегда strict `DLC=8`.
- [ ] Executor payload little-endian.
- [ ] Host binary response остается big-endian, где это задано Host API.
- [ ] `command_parser` только распознает команду, проверяет длину и выбирает direct handler или recipe.
- [ ] `parameter_parser` только разбирает Host payload в канонические Host-поля.
- [ ] `parameter_parser` не должен принимать физических решений типа `volume_ul -> duration_ms`.
- [ ] `calibrator` отдельно переводит Host-величины в физические параметры исполнителей.
- [ ] `ACK` не продвигает recipe.
- [ ] Executor `DONE` завершает только одну low-level atomic-команду.
- [ ] Host `DONE` отправляется только после завершения всего recipe/job.
- [ ] `ACK without DONE` считается fault/timeout, а не успехом.
- [ ] Дозирование насосом выполняется через `PUMP_RUN_DURATION`, не через `START -> WAIT -> STOP`.

Общий уровень экосистемы: CAN bitrate, 29-bit Extended ID, strict `DLC=8`, message/subtype registry, service commands `F001..F007`, NACK registry, endian, ACK/DATA/DONE ordering и timeout classes.

Специфика исполнителя: физические каналы, safe limits, калибровки, home/motion profile, pump flow model, sensor conversion time, scan profile и shared resource groups.

## 4. Этап A: синхронизация протокольных констант

**Цель:** локальный `can_packer.h` не должен расходиться с `dds240_global_config.h` по общим константам.

- [ ] Сверить `CAN_ADDR_*` с `DDS240_CAN_ADDR_*`.
- [ ] Сверить message types и response subtypes.
- [ ] Добавить явный `CAN_CMD_SRV_GET_UID = 0xF004`.
- [ ] Добавить явный `CAN_CMD_SRV_GET_STATUS = 0xF007`.
- [ ] Исправить NACK registry:
  - `[ ]` `0x0001 = UNKNOWN_CMD`
  - `[ ]` `0x0002 = INVALID_DEVICE_ID / INVALID_CHANNEL`
  - `[ ]` `0x0003 = DEVICE_BUSY`
  - `[ ]` `0x0004 = INVALID_KEY`
  - `[ ]` `0x0005 = FLASH_WRITE`
  - `[ ]` `0x0006 = INVALID_PARAM`
- [ ] Убрать конфликт старого смысла `0x0003 = DEVICE_FAILURE`.
- [ ] Оставить доменные алиасы, если они не противоречат global config.

**Приемка:**

- [ ] `can_packer.h` совпадает с global config по общим ID, service commands, NACK и magic keys.
- [ ] Сборка проходит.
- [ ] CANable fake responder продолжает принимать старые smoke-команды.

## 5. Этап B: command parser, parameter parser и calibrator boundary

**Цель:** разделить три ответственности: распознавание команды, разбор Host payload и физическую калибровочную модель.

Правильная цепочка:

```text
command_parser
  -> parameter_parser
  -> recipe/job layer
  -> calibrator
  -> device_mapping
  -> can_packer
```

### 5.1. `command_parser`

- [ ] Оставить в `command_parser.c` только таблицы команд, проверку длины, ACK/NACK и запуск direct/recipe пути.
- [ ] Проверить все `min_params_len/max_params_len` по `Commands_API/User_Commands/commands.md`.
- [x] Обновить комментарии descriptor-а `WASH_STATION_FILL`.
- [ ] Исключить из `command_parser.c` любые знания о low-level executor payload.

### 5.2. `parameter_parser`

- [ ] `parameter_parser.c` должен читать только Host payload в порядке документации.
- [ ] В `UniversalCommand_t` хранить канонические значения Host API:
  - `[x]` `volume_ul`
  - `[x]` `cuvette`
  - `[ ]` `slot`
  - `[ ]` `cycles`
  - `[ ]` `duration_ms`, только если Host API прямо задает длительность технологической операции.
- [x] Не вызывать `volume -> pump duration` внутри `parameter_parser`.
- [ ] Разрешены простые read helpers: `read_u16_be`, `read_i32_be` и т.п.
- [ ] Доменные расчеты шагов и времен вынести из parser-а в translator/calibrator слой поэтапно.

### 5.3. `calibrator`

- [x] Создать слой калибровки для перевода объема в время работы насоса.
- [x] Рекомендуемые файлы:

```text
App/Inc/Dispatcher/calibrator.h
App/Src/Dispatcher/calibrator.c
```

- [x] Ввести API:

```c
bool Calibrator_PumpVolumeToDurationMs(uint8_t pump_sys_id,
                                       uint16_t volume_ul,
                                       PumpOperation_t operation,
                                       uint32_t *duration_ms);
```

- [x] На первом этапе использовать статическую таблицу коэффициентов.
- [x] Если калибровка отсутствует, невалидна или дает `duration_ms == 0`, low-level CAN-команду не отправлять.
- [ ] Зафиксировать min/max допустимые `duration_ms`.
- [ ] Подготовить API так, чтобы позже заменить статическую таблицу на Flash/service calibration без изменения parser-а.

### 5.4. Найденный Host API разрыв

Критический найденный разрыв:

```text
WASH_STATION_FILL
Документация: volume:uint16, cuvette:uint16
Текущий parser: cuvette:uint16, volume:uint16
```

- [x] Исправить разбор `0x4100` в `parameter_parser.c`.
- [x] Проверить Python-тесты на порядок параметров.
- [x] Убедиться, что executor layout не используется как Host layout.

**Приемка:**

- [x] `command_parser.c` для `0x4100` ожидает 4 байта и не трактует их физически.
- [x] `parameter_parser.c` для `0x4100` извлекает `volume_ul=500`, `cuvette=10`.
- [x] `parameter_parser.c` не рассчитывает `pump_duration_ms`.
- [x] `Calibrator_PumpVolumeToDurationMs()` рассчитывает `duration_ms` из `volume_ul=500`.
- [x] Поворот/позиционирование строится из `cuvette=10` на translator/job layer.
- [x] Host ACK/DONE lifecycle не меняется.

## 6. Этап C: finite Fluidics action

**Цель:** заменить дозирование насосом на одну атомарную executor-команду.

Нужно перейти от:

```text
PUMP_START -> WAIT_MS(duration_ms) -> PUMP_STOP
```

к:

```text
PUMP_RUN_DURATION(ch_idx, duration_ms)
```

- [x] Добавить `ACTION_RUN_PUMP_DURATION` в `recipe_store.h`.
- [x] Добавить параметры action:
  - `[x]` `pump_id`
  - `[x]` `pump_id_source`
  - `[x]` `duration_ms`
  - `[x]` `duration_ms_source`
- [x] Добавить обработку action в `JobManager_ExecuteStep`.
- [x] Использовать `Packer_CreatePumpRunDurationMsg`.
- [x] Не отправлять команду, если `duration_ms == 0`.
- [x] При `duration_ms == 0` завершать Host/job ошибкой параметров.
- [x] Заменить `WASH_STATION_FILL` на один `ACTION_RUN_PUMP_DURATION`.
- [x] Заменить дозирующие шаги `WASH_STATION_WASH`.
- [x] Оставить `ACTION_START_PUMP` и `ACTION_STOP_PUMP` для manual/service flows.

**Приемка:**

- [x] `WASH_STATION_FILL` формирует CAN `0x0201 PUMP_RUN_DURATION`.
- [x] Для `WASH_STATION_FILL` не формируются `0x0202 PUMP_START` и `0x0203 PUMP_STOP`.
- [x] `WAIT_MS` не используется как основной механизм дозирования насосом.
- [x] Executor `DONE` по `PUMP_RUN_DURATION` продвигает только один atomic step.

## 7. Этап D: timeout model для atomic actions

**Цель:** уйти от одного грубого `JOB_TIMEOUT_MS` для всех физических операций.

- [~] Разделить понятия:
  - `[ ]` ACK timeout: 50 ms.
  - `[ ]` fast DONE timeout: 100 ms для быстрых state/service commands.
  - `[x]` operation timeout для finite Fluidics commands.
  - `[x]` operation timeout для finite Motion `ROTATE/HOME`.
- [x] Для `PUMP_RUN_DURATION` считать:

```text
operation_timeout_ms = duration_ms + transport_margin_ms + executor_margin_ms
```

- [x] Для `MOTOR_ROTATE` считать:

```text
operation_timeout_ms = abs(steps) / speed + motion_margin_ms
```

- [ ] Для всех executor families ввести timeout по контракту команды:

| Исполнитель | Команды | Timeout policy |
|:---|:---|:---|
| Fluidics | `PUMP_RUN_DURATION` | `duration_ms + margin` |
| Fluidics | `PUMP_START/STOP`, `VALVE_OPEN/CLOSE` | fast/state timeout |
| Motion | `MOTOR_ROTATE` | `abs(steps) / speed + motion margin` |
| Motion | `MOTOR_HOME` | home-profile timeout, не fast timeout |
| Motion | `START_CONTINUOUS/STOP` | fast/state timeout |
| Thermo | `GET_TEMP`, `GET_ALL_TEMPS` | sensor conversion/read timeout |
| Photometer | scan commands | optical scan/profile timeout |
| Mixer | future finite mix command | `duration_ms + margin` |

- [x] Для `steps != 0 && speed == 0` не отправлять Motion-команду.
- [x] При отсутствии `DONE` до step operation timeout завершать job как timeout/fault.

**Приемка:**

- [x] `PUMP_RUN_DURATION 2000 ms` не падает по fast timeout.
- [x] `ACK without DONE` не считается успешным step.
- [ ] Host получает error/DONE status согласно job result.

## 8. Этап E: корреляция executor responses

**Цель:** не продвигать job чужим или сервисным ответом.

Текущий компромисс: `MAX_CONCURRENT_JOBS=1`, ответ привязывается к первому running job.

- [ ] Ввести структуру pending atomic action.
- [ ] Хранить expected tuple:

```text
source_addr, command_code, ch_idx
```

- [ ] `DONE/NACK` сопоставлять с pending action.
- [ ] Не продвигать recipe по service DATA/DONE, если это discovery/status transaction.
- [ ] Логировать дублирующие и неожиданные responses.

**Приемка:**

- [ ] `F001/F007` не завершают recipe action.
- [ ] `DONE` от неправильного `ch_idx` не продвигает step.
- [ ] Код готов к будущему `MAX_CONCURRENT_JOBS > 1`.

## 9. Этап F: Discovery и ServiceManager

**Цель:** inventory должен строиться из `GET_DEVICE_INFO DATA`, а `DONE` только завершает transaction.

- [ ] Перенести `ServiceManager_UpdateNode()` на обработку DATA по `F001`.
- [ ] Парсить первый DATA frame:
  - `[ ]` `device_type`
  - `[ ]` `fw_major`
  - `[ ]` `fw_minor`
  - `[ ]` `channel_count`
  - `[ ]` UID fragments
- [ ] Добавить поддержку `F004 GET_UID`.
- [ ] Добавить поддержку `F007 GET_STATUS`.
- [ ] Ввести baseline/delta для status metrics.
- [ ] После `REBOOT/FACTORY_RESET/SET_NODE_ID` выполнять recovery discovery.

**Приемка:**

- [ ] Motion `0x20` определяется как `device_type=0x01`, `channels=8`.
- [ ] Fluidics `0x30` определяется как `device_type=0x03`, `channels=16`.
- [ ] Thermo `0x40` определяется как `device_type=0x02`, `channels=8`.
- [ ] `INIT` блокируется, если требуемый node offline.

## 10. Этап G: Motion resource model

**Цель:** планировщик Дирижера должен учитывать реальные shared timer groups Motion Executor.

- [ ] Ввести resource groups:
  - `[ ]` `TIM1 group = motor 0..3`
  - `[ ]` `TIM2 group = motor 4..7`
- [ ] Проверять конфликт групп перед параллельным step.
- [ ] Для `MOTOR_ROTATE steps != 0 && speed == 0` возвращать ошибку до CAN.
- [ ] `STOP` не считать штатным завершением `ROTATE`.
- [ ] `START_CONTINUOUS + STOP` оставить как state/manual flow.

**Приемка:**

- [ ] Две команды внутри одной TIM group не стартуют параллельно.
- [ ] Команды в разных TIM groups могут идти параллельно, если recipe это разрешает.
- [ ] `MOTOR_BUSY` от Motion трактуется как невыполненный step.

## 11. Этап H: тестовая инфраструктура

- [x] Обновить ожидания логов `test_main_processes.py` с `ID:...` на `Phys:<node>:<channel>`.
- [ ] Добавить selective fake nodes:
  - `[ ]` fake Motion only
  - `[ ]` fake Fluidics only
  - `[ ]` fake Thermo only
- [x] Добавить тест `WASH_STATION_FILL -> PUMP_RUN_DURATION`.
- [ ] Добавить тест, что `duration_ms=0` не отправляется executor-у.
- [ ] Добавить тест, что Motion `speed=0` при `steps!=0` не отправляется.
- [ ] Добавить тесты `F001/F004/F007` multi-frame ordering.

**Приемка:**

- [x] Fake executor smoke PASS.
- [ ] Selective fake Fluidics + real Motion сценарий возможен.
- [ ] Selective fake Motion + real Fluidics сценарий возможен.

## 12. Рекомендуемый порядок патчей

### Патч 1: минимальный Fluidics boundary

- [x] Исправить `WASH_STATION_FILL volume,cuvette` в Host parameter parser.
- [x] Убрать расчет `pump_duration_ms` из `parameter_parser.c`.
- [x] Создать минимальный `calibrator.h/.c`.
- [x] Подключить расчет `volume_ul -> duration_ms` через calibrator.
- [x] Добавить `ACTION_RUN_PUMP_DURATION`.
- [x] Перевести `WASH_STATION_FILL` на `PUMP_RUN_DURATION`.
- [x] Обновить тест/ожидание CAN кадра.

### Патч 2: протокольная синхронизация

- [ ] Синхронизировать `can_packer.h` с `dds240_global_config.h`.
- [ ] Добавить `F004/F007`.
- [ ] Исправить NACK registry.

### Патч 3: timeout для finite commands

- [x] Добавить operation timeout per step/action.
- [ ] Настроить `PUMP_RUN_DURATION`.
- [x] Настроить `MOTOR_ROTATE`.

### Патч 4: Discovery/ServiceManager

- [ ] Перенести inventory update на DATA.
- [ ] Добавить UID/status baseline.

### Патч 5: Motion resource model

- [ ] Ввести shared group locks.
- [ ] Добавить локальную валидацию speed/steps.

### Патч 6: группа команд миксера `0x30xx`

- [x] Скорректировать `MIXER_MIX (0x3100)`: parser хранит Host `mixer_id,cuvette,duration,wash_cycles`, без расчета шагов.
- [x] Перевести `MIXER_MIX` на смысловые `PARAM_SOURCE_MIXER_*`.
- [ ] Завести `MIXER_WASH (0x3000)` как отдельную Host-команду и recipe.
- [ ] Завести `MIXER_HOME (0x3200)` как отдельную Host-команду и recipe.
- [ ] Подключить `wash_cycles` из `MIXER_MIX (0x3100)` к общей логике промывки миксера.
- [ ] До закрытия `MIXER_WASH` ввести общий механизм повторения `cycles` или явный recipe-loop для циклических команд.

### Патч 7: очистка `RecipeID_t`

- [ ] После стабилизации группы миксера проверить старые recipe ID строкового/демо-режима.
- [ ] Убрать `RECIPE_START_MOTOR`, если строковая команда `CMD_START_MOTOR` больше не нужна.
- [ ] Отдельно решить судьбу `RECIPE_ASPIRATE`: оставить как legacy diagnostics или заменить актуальным Host recipe.
- [ ] Держать в `RecipeID_t` только реальные Host recipes и явно помеченные service/diagnostic recipes.

### Патч 8: команды ротора реагентов `0x5100/0x5200/0x5300`

- [ ] Проверить Host API раздел `0x50xx` перед реализацией: `REAGENT_SCAN_BARCODE`, `REAGENT_GET_TEMP`, `REAGENT_SET_TEMP`.
- [ ] Завести parsed-структуры без потери Host-параметров: `rotor_id`, `slot`, `temperature`.
- [ ] Отдельно определить маршрут выполнения: barcode/temp команды требуют DATA-ответов и могут отличаться от обычных motor recipe.
- [ ] Реализовывать этот блок отдельно от уже закрытого `REAGENT_ROTATE (0x5000)`.

### Патч 9: группа фотометра `0x60xx`

- [x] Скорректировать `PHOTOMETER_SCAN_SINGLE (0x6100)`: parser хранит Host `cuvette,wavelengths`, без расчета шагов.
- [x] Использовать общий `PARAM_SOURCE_REACTION_DISK_ROTATE_STEPS` для подвода кюветы реакционным диском.
- [x] Убрать фотометрический перевод `cuvette -> steps`, чтобы не было второй модели позиционирования кюветы.
- [ ] Отдельно реализовать `PHOTOMETER_SCAN_ALL (0x6000)`, `PHOTOMETER_CALIBRATE (0x6200)`, `PHOTOMETER_GET_WAVELENGTHS (0x6300)`.

### Патч 10: группа реакционного диска `0x70xx`

- [ ] Завести `REACTION_ROTATE (0x7000)` как отдельную Host-команду: `cuvette,position`.
- [ ] Завести `REACTION_HOME (0x7100)` как отдельную Host-команду без параметров.
- [ ] До реализации `0x7000` определить таблицу рабочих позиций реакционного диска: `0=фотометр`, `1=дозатор`, `2=миксер`, `3=моющая станция`.
- [ ] Расчет `cuvette,position -> steps` должен учитывать смещение рабочей позиции; терять `position` нельзя.

## 13. Журнал исполнения

| Дата | Маркер | Запись |
|:---|:---:|:---|
| 2026-05-05 | `[x]` | План рефакторинга создан. Кодовые правки не выполнялись. |
| 2026-05-05 | `[x]` | Зафиксирован режим: инженер правит код, AI-ассистент ведет документацию и консультирует. |
| 2026-05-05 | `[x]` | Инженер развел parser boundary для `WASH_STATION_FILL`: parser хранит `volume_ul/cuvette`, без расчета `pump_duration_ms`. |
| 2026-05-05 | `[x]` | Инженер добавил минимальный calibrator и перенес расчет `cuvette -> steps`, `volume_ul -> duration_ms` из parser-а в job/translator/calibrator boundary. |
| 2026-05-05 | `[x]` | Контрольная сборка после parser/calibrator boundary проходит. |
| 2026-05-05 | `[x]` | `WASH_STATION_FILL` переведен на finite action `ACTION_RUN_PUMP_DURATION`; сборка проходит, размер ELF: text 80904, data 348, bss 46412. |
| 2026-05-05 | `[x]` | Добавлен per-step timeout и margin для `PUMP_RUN_DURATION`; проект собирается. |
| 2026-05-05 | `[x]` | `test_main_processes.py` обновлен: `WASH_STATION_FILL` отправляет `volume,cuvette` и ожидает `RUN_PUMP_DURATION`. |
| 2026-05-05 | `[x]` | Все насосные recipe-дозирования переведены на `ACTION_RUN_PUMP_DURATION`; `START/STOP_PUMP` остаются только как service/manual actions; проект собирается. |
| 2026-05-06 | `[x]` | CANable fake-executor E2E PASS: `WASH_STATION_FILL` и `WASH_STATION_WASH` проходят через `PUMP_RUN_DURATION`, Host получает DONE. |
| 2026-05-06 | `[x]` | Ожидания логов в `test_main_processes.py` обновлены под текущий формат `Phys:<node>:<channel>` и `SysID`. |
| 2026-05-06 | `[x]` | Clean CANable E2E PASS: полный `test_main_processes.py` проходит без warning по ожиданиям логов. |
| 2026-05-06 | `[x]` | Зафиксирован принцип: общие правила DDS-240 обязательны для Дирижера и всех исполнителей; специфика платы допускается только как параметризация общего контракта. |
| 2026-05-06 | `[x]` | Motion timeout contract внедрен для `ROTATE/HOME`; `steps != 0 && speed == 0` блокируется до CAN; fake CAN E2E regression PASS. |
| 2026-05-06 | `[x]` | Начата нормализация `ParamSource_t`: блок дозатора переведен с command-specific источников на смысловые `PARAM_SOURCE_DISPENSER_*`; рецепты `DISPENSER_WASH/ASPIRATE/DISPENSE` используют общие параметры устройства. |
| 2026-05-06 | `[x]` | Блок моющей станции переведен на смысловые `PARAM_SOURCE_WASH_STATION_*`; Host payload `WASH_STATION_WASH cycles,cuvette` и `WASH_STATION_FILL volume,cuvette` сохранен без изменения. |
| 2026-05-06 | `[x]` | Блок диска образцов/реагентов переведен на смысловой `PARAM_SOURCE_REAGENT_SAMPLE_ROTATE_STEPS`; Host payload `SAMPLE_ROTATE slot` и `REAGENT_ROTATE rotor_id,slot` сохранен без изменения. |
| 2026-05-06 | `[x]` | Проверен Host API раздел `0x50xx`: `0x5100/0x5200/0x5300` есть в документации, но не входят в текущий реализованный блок; добавлены как отдельная будущая работа. |
| 2026-05-06 | `[x]` | Блок `PHOTOMETER_SCAN_SINGLE` скорректирован: `cuvette` хранится как Host-параметр, поворот выполняет реакционный диск через общий `PARAM_SOURCE_REACTION_DISK_ROTATE_STEPS`. |
| 2026-05-06 | `[x]` | Проверен Host API раздел `0x70xx`: `REACTION_ROTATE` требует `cuvette,position`; кодовый блок отложен до фиксации смещений рабочих позиций реакционного диска. |
| 2026-05-06 | `[x]` | Блок `MIXER_MIX` скорректирован: Host-поля сохраняются в parser, шаги XY/Z рассчитываются в `JobManager` через смысловые `PARAM_SOURCE_MIXER_*`. |
| 2026-05-06 | `[x]` | CANable fake-executor E2E PASS после коррекции миксера: `MIXER_MIX` выполняет XY, Z down, `RUN_PUMP_DURATION` Fluidics ch 12, Z up и home XY. |
| 2026-05-07 | `[x]` | Текущий блок Дирижера закрыт перед переходом к Thermo: Motion/Fluidics считаются приведенными к экосистеме, Дирижер прошел fake-executor regression, Sensor Executor зафиксирован отдельным ТЗ и интеграционным разделом. |
