# Техническая документация: Прошивка дирижера ДДС-240

**Проект:** STM32H723_mother_board
**Устройство:** Биохимический анализатор ДДС-240, плата дирижера
**MCU:** STM32H723ZG (ARM Cortex-M7 @ 550 MHz)
**Дата аудита:** 02.03.2026
**Версия прошивки:** см. git log (ветка main)

---

## 1. Общее описание системы

Плата дирижера (Mother Board) является центральным управляющим узлом биохимического анализатора ДДС-240. Она принимает высокоуровневые команды от хост-ПК по USB (CDC Virtual COM), декомпозирует их в последовательности атомарных действий (рецепты) и отправляет управляющие команды на исполнительные модули по шине CAN (FDCAN).

### Архитектура «Host-Controlled Workflow»

```
┌─────────┐      USB CDC       ┌──────────────┐     FDCAN      ┌──────────────┐
│  Хост-  │  ◄──────────────►  │  Дирижер     │  ───────────►  │ Исполнители  │
│  ПК     │  Бинарный протокол │  (STM32H723) │  CAN-кадры    │ (моторы,     │
│         │                    │              │  ◄───────────  │  насосы,      │
└─────────┘                    └──────────────┘                │  датчики)    │
                                                               └──────────────┘
```

- Хост-ПК управляет последовательностью операций анализа
- Дирижер — однозадачный исполнитель (1 команда одновременно)
- При занятости возвращает ERR_BUSY (0x0004)
- Сложная логика анализа реализуется на стороне хоста

---

## 2. Аппаратная платформа

| Параметр | Значение |
|----------|----------|
| MCU | STM32H723ZG (LQFP-144) |
| Ядро | ARM Cortex-M7, FPU, DSP |
| Тактовая частота | 550 MHz (настроено ~520 MHz) |
| Flash | 1 MB |
| RAM | 564 KB (DTCM 128KB + AXI SRAM 320KB + SRAM1/2/4) |
| USB | USB FS (CDC Virtual COM Port) |
| CAN | FDCAN1 (Classic CAN mode) |
| GPIO | LED на PB14 (статус) |

### Конфигурация тактирования

- HSI: 64 MHz
- PLL: PLLN=32, PLLM=4, PLLP=1, PLLQ=4
- AHB Prescaler: /2
- APB1/2/3/4 Prescaler: /2

---

## 3. Программная архитектура

### 3.1. RTOS и задачи

Прошивка построена на **FreeRTOS** с оберткой **CMSIS-RTOS v2**.

| Задача | Приоритет | Стек (слов) | Файл | Назначение |
|--------|-----------|-------------|------|------------|
| task_watchdog | High | 128 | task_watchdog.c | Сторожевой таймер (заглушка) |
| task_usb_handler | High2 | 512 | task_usb_handler.c | Передача USB TX из очереди |
| task_can_handler | High1 | 256 | task_can_handler.c | Прием/передача CAN-сообщений |
| task_dispatcher | BelowNormal | 2048 | task_dispatcher.c | Сборка пакетов, парсинг, диспетчеризация |
| task_logger | Low1 | 256 | task_logger.c | Системное логирование (заглушка) |
| task_jobs_monitor | Low | 128 | task_jobs_monitor.c | Мониторинг выполнения заданий |

### 3.2. Межзадачное взаимодействие

```
                    ┌─────────────────────────────────┐
                    │         USB CDC (ПК)            │
                    └──────┬───────────────┬──────────┘
                           │               ▲
                    ▼               │
              usb_rx_stream_buffer     usb_tx_queue
              (1024 байт)              (10 × 256 байт)
                           │               ▲
                    ▼               │
              ┌────────────────────────────────┐
              │       task_dispatcher          │
              │  (сборка пакетов + парсинг)    │
              └────────┬───────────────────────┘
                       │
                       ▼
              ┌────────────────────────────────┐
              │       JobManager               │
              │  (выполнение рецептов)         │
              └────────┬───────────────────────┘
                       │
              can_tx_queue ──────► task_can_handler ──► FDCAN1
              (20 × 8 байт)
              can_rx_queue ◄────── task_can_handler ◄── FDCAN1
              (20 × 8 байт)
```

| Ресурс | Тип | Размер | Назначение |
|--------|-----|--------|------------|
| usb_rx_stream_buffer | StreamBuffer | 1024 байт | Поток сырых USB-данных |
| usb_rx_queue | Queue | 10 × 256 байт | Собранные USB-команды |
| usb_tx_queue | Queue | 10 × USB_TxPacket_t | Ответы для отправки по USB |
| can_rx_queue | Queue | 20 × 8 байт | Входящие CAN-сообщения |
| can_tx_queue | Queue | 20 × 8 байт | Исходящие CAN-сообщения |
| log_queue | Queue | 30 × 128 байт | Сообщения лога |
| usb_tx_sem | Semaphore | Binary | Синхронизация USB TX |

---

## 4. Протокол связи Host ↔ Дирижер (USB)

### 4.1. Формат пакета команды

```
┌───────────┬──────────┬───────────┬────────────┬───────┐
│  Заголовок │  Длина   │  Команда  │ Параметры  │  CRC  │
│  3 байта   │ 2 байта  │  2 байта  │  0..N байт │1 байт │
│  CM> (hex: │ Big-End. │ Big-End.  │            │ XOR   │
│  43 4D 3E) │          │           │            │       │
└───────────┴──────────┴───────────┴────────────┴───────┘
```

- **Заголовок:** `0x43 0x4D 0x3E` (ASCII: `CM>`)
- **Длина:** 2 байта big-endian — количество байт после поля длины (команда + параметры + CRC)
- **Команда:** 2 байта big-endian — код команды
- **Параметры:** переменная длина (0..N байт)
- **CRC:** 1 байт — XOR всех байт от команды до последнего параметра

### 4.2. Формат пакета ответа

```
┌───────────┬──────────┬───────────┬───────┬──────────┬──────────┬───────┐
│  Заголовок │  Длина   │  Команда  │  Тип  │  Статус  │  Данные  │  CRC  │
│  3 байта   │ 2 байта  │  2 байта  │1 байт │ 2 байта  │  0..N    │1 байт │
└───────────┴──────────┴───────────┴───────┴──────────┴──────────┴───────┘
```

### 4.3. Типы ответов

| Код | Имя | Описание |
|-----|-----|----------|
| 0x00 | NACK | Ошибка формата пакета (CRC, длина) |
| 0x01 | ACK | Команда принята, выполнение начато |
| 0x02 | DONE | Команда выполнена успешно |
| 0x03 | DATA | Передача данных |
| 0x04 | ERROR | Ошибка выполнения команды |

### 4.4. Типичные последовательности

**Команда без данных:**
```
Host  ──CMD──►  Дирижер
Host  ◄──ACK──  Дирижер
      (выполнение...)
Host  ◄──DONE─  Дирижер
```

**Команда с данными:**
```
Host  ──CMD──►  Дирижер
Host  ◄──ACK──  Дирижер
      (выполнение...)
Host  ◄──DATA─  Дирижер
Host  ◄──DONE─  Дирижер
```

**Ошибка формата:**
```
Host  ──CMD──►  Дирижер  (CRC не совпал)
Host  ◄──NACK─  Дирижер
```

**Ошибка выполнения:**
```
Host  ──CMD──►  Дирижер
Host  ◄──ACK──  Дирижер
      (ошибка при выполнении...)
Host  ◄──ERROR  Дирижер  (с кодом ошибки)
```

### 4.5. Тайминги

| Параметр | Значение |
|----------|----------|
| Baudrate USB CDC | 9600 (логический) |
| Таймаут ACK | 500 мс |
| Таймаут DONE | 60 с |
| Таймаут шага рецепта | 5000 мс |

---

## 5. Реестр команд

### 5.1. Системные команды (0x10xx)

| Код | Имя | Параметры | Ответ | Статус |
|-----|-----|-----------|-------|--------|
| 0x1000 | GET_STATUS | — | DATA: state(1B) + error_code(2B) | **Реализована** |
| 0x1001 | RESET | — | DONE | Не реализована |
| 0x1002 | INIT | modules_mask(1B) | DONE | **Реализована** |
| 0x1003 | GET_VERSION | — | DATA: version_info | Не реализована |
| 0x1010 | EMERGENCY_STOP | — | DONE | Не реализована |

**Маска модулей для INIT (modules_mask):**

| Бит | Значение | Модуль |
|-----|----------|--------|
| 0 | 0x01 | Дозаторы |
| 1 | 0x02 | Миксеры |
| 2 | 0x04 | Станция промывки |
| 3 | 0x08 | Ротор реагентов |
| 4 | 0x10 | Диск образцов |
| 5 | 0x20 | Реакционный диск |
| 6 | 0x40 | Фотометр |
| 7 | 0x80 | Термостаты |
| — | 0xFF | Все модули |

### 5.2. Команды дозатора (0x20xx)

| Код | Имя | Параметры | Статус |
|-----|-----|-----------|--------|
| 0x2000 | DISPENSER_WASH | disp_id(1B), volume(2B), cycles(1B) | **Реализована** |
| 0x2100 | DISPENSER_ASPIRATE | disp_id(1B), source_type(1B), slot(2B), volume(2B) | **Реализована** |
| 0x2200 | DISPENSER_DISPENSE | disp_id(1B), target_type(1B), slot(2B), volume(2B) | **Реализована** |
| 0x2300 | DISPENSER_HOME | disp_id(1B) | Не реализована |
| 0x2400 | DISPENSER_MOVE | disp_id(1B), target(1B), slot(2B), z_offset(2B) | Не реализована |

**Типы источников/назначений (source_type / target_type):**

| Код | Тип |
|-----|-----|
| 0x01 | Реакционный диск (кюветы) |
| 0x02 | Ротор реагентов |
| 0x03 | Диск образцов |
| 0x04 | Станция промывки |
| 0x05 | Сброс (waste) |

### 5.3. Команды миксера (0x30xx)

| Код | Имя | Параметры | Статус |
|-----|-----|-----------|--------|
| 0x3000 | MIXER_WASH | mixer_id(1B), cycles(1B) | Не реализована |
| 0x3100 | MIXER_MIX | mixer_id(1B), cuvette(2B), duration(2B), wash_cycles(1B) | **Реализована** |
| 0x3200 | MIXER_HOME | mixer_id(1B) | Не реализована |

### 5.4. Команды станции промывки (0x40xx)

| Код | Имя | Параметры | Статус |
|-----|-----|-----------|--------|
| 0x4000 | WASH_STATION_WASH | cycles(1B), cuvette(2B) | **Реализована** |
| 0x4100 | WASH_STATION_FILL | cuvette(2B), volume(2B) | **Реализована** |
| 0x4200 | WASH_STATION_DRAIN | cuvette(2B) | Не реализована |

### 5.5. Команды роторов (0x50xx)

| Код | Имя | Параметры | Статус |
|-----|-----|-----------|--------|
| 0x5000 | REAGENT_ROTATE | rotor_id(1B), slot(2B) | **Реализована** |
| 0x5100 | REAGENT_SCAN_BARCODE | rotor_id(1B), slot(2B) | Не реализована |
| 0x5110 | SAMPLE_ROTATE | slot(2B) | **Реализована** |
| 0x5120 | SAMPLE_SCAN_BARCODE | slot(2B) | Не реализована |

### 5.6. Команды фотометра (0x60xx)

| Код | Имя | Параметры | Статус |
|-----|-----|-----------|--------|
| 0x6000 | PHOTOMETER_SCAN_ALL | wavelengths_mask(1B) | Не реализована |
| 0x6100 | PHOTOMETER_SCAN_SINGLE | cuvette(2B), wavelengths(1B) | **Реализована** |
| 0x6200 | PHOTOMETER_CALIBRATE | type(1B), wavelengths(1B) | Не реализована |

### 5.7. Реакционный диск (0x70xx) — Не реализованы

| Код | Имя | Параметры |
|-----|-----|-----------|
| 0x7000 | REACTION_ROTATE | cuvette(2B), position(1B) |
| 0x7100 | REACTION_HOME | — |

### 5.8. Термоконтроль (0x80xx) — Не реализованы

| Код | Имя | Параметры |
|-----|-----|-----------|
| 0x8000 | THERMO_GET_TEMP | thermo_id(1B) |
| 0x8001 | THERMO_SET_TEMP | thermo_id(1B), temperature(2B) |
| 0x8002 | THERMO_START | thermo_id(1B) |
| 0x8003 | THERMO_STOP | thermo_id(1B) |
| 0x8020 | THERMO_REACTION_TEMP | action(1B), temperature(2B) |
| 0x8030 | THERMO_REAGENT_TEMP | rotor_id(1B), action(1B), temperature(2B) |
| 0x8040 | THERMO_SAMPLE_TEMP | action(1B), temperature(2B) |

### 5.9. Датчики (0x90xx) — Не реализованы

Зарезервированы коды для датчиков уровня жидкости, температуры, положения, крышки и др.

---

## 6. Архитектура диспетчера (Dispatcher)

### 6.1. Конвейер обработки команды

```
USB-байты ─► StreamBuffer ─► task_dispatcher (сборка пакета)
                                      │
                                      ▼
                              Parser_ProcessBinaryCommand()
                                      │
                          ┌───────────┴───────────┐
                          ▼                       ▼
                   Прямая команда          Команда-рецепт
                   (DirectCmd Table)       (RecipeCmd Table)
                          │                       │
                          ▼                       ▼
                   handler(code,           Parameters_Parse()
                   params, len)                   │
                          │                       ▼
                          │               ParamTranslator_*()
                          │                       │
                          │                       ▼
                          │               JobManager_StartNewJob()
                          │                       │
                          ▼                       ▼
                   Dispatcher_Send*()     recipe → CAN-кадры → исполнители
```

### 6.2. Модули диспетчера

| Модуль | Файл | Строк | Назначение |
|--------|------|-------|------------|
| Command Parser | command_parser.c/h | 366+234 | Парсинг бинарных пакетов, маршрутизация |
| Dispatcher I/O | dispatcher_io.c/h | 208+73 | Формирование и отправка USB-ответов |
| Parameter Parser | parameter_parser.c/h | 237 | Парсинг параметров из сырых байт |
| Param Translator | param_translator.c/h | 281+241 | Трансляция параметров → аппаратные значения |
| Job Manager | job_manager.c/h | 526+55 | Машина состояний выполнения рецептов |
| Recipe Store | recipe_store.c/h | 710+184 | Хранилище рецептов (Flash) |
| CAN Packer | can_packer.c/h | 50 | Упаковка действий в CAN-кадры |
| Direct Cmd Handlers | direct_command_handlers.c/h | 45 | Обработчики прямых команд |

### 6.3. Типы данных ядра

**UniversalCommand_t** — центральная структура для разобранной команды:

```c
typedef struct {
    uint16_t command_code;     // Код команды (0x1000, 0x2000, ...)
    RecipeID_t recipe_id;      // ID рецепта
    enum { ARGS_TYPE_NONE, ARGS_TYPE_STRING, ARGS_TYPE_BINARY, ARGS_TYPE_PARSED } args_type;
    union {
        char string[256];
        BinaryArgs_t binary;
        ParsedArgs_Init init;
        ParsedArgs_DispenserWash dispenser_wash;
        ParsedArgs_DispenserAspirate dispenser_aspirate;
        ParsedArgs_DispenserDispense dispenser_dispense;
        ParsedArgs_MixerMix mixer_mix;
        ParsedArgs_WashStationWash wash_station_wash;
        ParsedArgs_WashStationFill wash_station_fill;
        ParsedArgs_SampleRotate sample_rotate;
        ParsedArgs_ReagentRotate reagent_rotate;
        ParsedArgs_PhotometerScanSingle photometer_scan_single;
    } args;
} UniversalCommand_t;
```

**JobContext_t** — контекст выполняемого задания:

```c
typedef struct {
    uint32_t job_id;
    JobStatus_t status;           // IDLE / RUNNING / COMPLETED / TIMEOUT / ERROR
    RecipeID_t initial_recipe_id;
    const ProcessStep_t* current_recipe;
    uint8_t current_step_index;
    uint8_t pending_actions_count;
    uint32_t step_start_time_ms;
    UniversalCommand_t initial_cmd;
} JobContext_t;
```

---

## 7. Система рецептов

### 7.1. Концепция

Каждая высокоуровневая команда от хоста транслируется в **рецепт** — последовательность **шагов**, где каждый шаг содержит одно или несколько **атомарных действий**, выполняемых параллельно.

```
Рецепт
  ├── Шаг 1: [действие A, действие B]  ← параллельно
  ├── Шаг 2: [действие C]              ← после завершения шага 1
  ├── Шаг 3: [действие D, действие E]  ← после завершения шага 2
  └── Маркер конца (NULL)
```

### 7.2. Атомарные действия (ActionType_t)

| ID | Действие | Описание |
|----|----------|----------|
| ACTION_NONE | — | Маркер конца рецепта |
| ACTION_ROTATE_MOTOR | Вращение мотора | motor_id, steps, speed |
| ACTION_START_PUMP | Включить насос | pump_id |
| ACTION_STOP_PUMP | Выключить насос | pump_id |
| ACTION_WAIT_MS | Задержка | delay_ms |
| ACTION_HOME_MOTOR | Поиск дома | motor_id, speed |
| ACTION_START_MIXING_MOTOR | Включить мотор миксера | mixer_id |
| ACTION_STOP_MIXING_MOTOR | Выключить мотор миксера | mixer_id |
| ACTION_PERFORM_SCAN | Сканирование фотометром | photometer_id, wavelength_mask |

### 7.3. Источники параметров (ParamSource_t)

Каждый параметр атомарного действия имеет поле `_source`, определяющее, откуда берется значение:

- **PARAM_SOURCE_STATIC** — значение вписано в рецепт (compile-time)
- **PARAM_SOURCE_CMD_**** — значение вычисляется из параметров команды пользователя (run-time)

Примеры динамических источников:
- `PARAM_SOURCE_CMD_DISPENSER_WASH_ROTATE_STEPS` — шаги из `ParsedArgs_DispenserWash.rotate_steps`
- `PARAM_SOURCE_CMD_PHOTOMETER_SCAN_SINGLE_WAVELENGTH_MASK` — маска длин волн из команды

### 7.4. Реализованные рецепты

| RecipeID | Команда | Шаги | Описание |
|----------|---------|------|----------|
| RECIPE_INITIALIZE_SYSTEM | 0x1002 | 2 | Home Motor 2 (игла) → Home Motor 1 (дозатор) |
| RECIPE_DISPENSER_WASH | 0x2000 | 6 | Поворот к станции → опускание → насос → подъем → home |
| RECIPE_DISPENSER_ASPIRATE | 0x2100 | 5 | Поворот → опускание → насос (забор) → подъем → home |
| RECIPE_DISPENSER_DISPENSE | 0x2200 | 5 | Поворот → опускание → насос (выдача) → подъем → home |
| RECIPE_MIXER_MIX | 0x3100 | 6 | Поворот к кювете → опускание → перемешивание → подъем → home |
| RECIPE_WASH_STATION_WASH | 0x4000 | 5 | Поворот → заполнение → слив → цикл |
| RECIPE_WASH_STATION_FILL | 0x4100 | 3 | Поворот → насос заполнения → остановка |
| RECIPE_SAMPLE_ROTATE | 0x5110 | 1 | Поворот диска образцов на N шагов |
| RECIPE_REAGENT_ROTATE | 0x5000 | 1 | Поворот ротора реагентов на N шагов |
| RECIPE_PHOTOMETER_SCAN_SINGLE | 0x6100 | 2 | Поворот реакц. диска → сканирование |

---

## 8. CAN-протокол (Дирижер ↔ Исполнители)

### 8.1. Физический уровень

- Интерфейс: FDCAN1 в режиме Classic CAN (не FD)
- Скорость: настраивается в CubeMX
- Кадр: стандартный (11-бит ID) или расширенный (29-бит ID)
- Данные: 8 байт на кадр

### 8.2. Структура CAN-сообщения

```c
typedef struct {
    FDCAN_TxHeaderTypeDef Header;  // HAL-заголовок
    uint8_t Data[8];               // Полезная нагрузка
} CanMessage_t;
```

### 8.3. Низкоуровневые команды исполнителям (CommandID_t)

| Код | Имя | Описание |
|-----|-----|----------|
| 0x01 | CMD_MOVE_ABSOLUTE | Абсолютное перемещение |
| 0x02 | CMD_MOVE_RELATIVE | Относительное перемещение (шаги) |
| 0x03 | CMD_SET_SPEED | Установить скорость |
| 0x04 | CMD_SET_ACCELERATION | Установить ускорение |
| 0x05 | CMD_STOP | Остановить движение |
| 0x06 | CMD_GET_STATUS | Запросить статус |
| 0x07 | CMD_SET_CURRENT | Установить рабочий ток |
| 0x08 | CMD_ENABLE_MOTOR | Включить/выключить драйвер |
| 0x09 | CMD_PERFORMER_ID_SET | Установить ID исполнителя |
| 0x10 | CMD_SET_PUMP_STATE | Включить/выключить насос |
| 0x11 | CMD_SET_VALVE_STATE | Открыть/закрыть клапан |
| 0x12 | CMD_GET_TEMPERATURE | Запросить температуру |

---

## 9. Конфигурация оборудования (заглушки для калибровки)

### 9.1. Моторы

| ID | Назначение | Примечание |
|----|------------|------------|
| 1 | Поворот дозатора (X-Y) | Горизонтальное перемещение |
| 2 | Подъем/опускание иглы (Z) | Вертикальное перемещение |
| 3 | Поворот реакционного диска | |
| 4 | Поворот диска образцов | |
| 5 | Карусель дозатора | |
| 6 | Ротор реагентов | |
| 8 | Поворот миксера (X-Y) | |
| 9 | Подъем/опускание лопатки (Z) | |
| 10 | Мотор перемешивания | Вращение лопатки |

### 9.2. Насосы

| ID | Назначение |
|----|------------|
| 0/1 | Насос дозатора (aspirate/dispense) |
| 2 | Насос заполнения станции промывки |
| 3 | Насос слива станции промывки |

### 9.3. Калибровочные константы (param_translator.h)

| Константа | Значение | Описание |
|-----------|----------|----------|
| SAMPLE_DISK_STEPS_PER_SLOT | 100 | Шагов/слот диска образцов |
| DISPENSER_ROT_STEPS_PER_SLOT | 200 | Шагов/слот поворота дозатора |
| DISPENSER_Z_STEPS_DOWN | 300 | Шагов опускания иглы |
| DISPENSER_Z_STEPS_UP | -300 | Шагов подъема иглы |
| PUMP_MS_PER_UL | 10 | мс/мкл работы насоса |
| ROTOR_REAGENT_STEPS_PER_SLOT | 100 | Шагов/слот ротора реагентов |
| MIXER_Z_STEPS_DOWN | 200 | Шагов опускания лопатки |
| MIXER_Z_STEPS_UP | -200 | Шагов подъема лопатки |
| MIXER_ROT_STEPS_PER_CUVETTE | 100 | Шагов/кювету поворота миксера |
| PHOTOMETER_STEPS_PER_CUVETTE | 100 | Шагов/кювету фотометра |
| PT_REACTION_DISK_STEPS_PER_CUVETTE | 100 | Шагов/кювету реакц. диска |
| PT_REACTION_DISK_MAX_CUVETTE | 40 | Макс. кювет на диске |
| DISPENSER_ROT_STEPS_TO_WASH_STATION | 2000 | Шагов к станции промывки |
| DISPENSER_Z_STEPS_DOWN_WASH_STATION | 500 | Опускание на станции промывки |

> **Все значения — заглушки!** Требуют калибровки на реальном оборудовании.

---

## 10. Коды ошибок

| Код | Имя | Описание |
|-----|-----|----------|
| 0x0000 | OK | Успех |
| 0x0001 | ERR_GENERIC | Общая ошибка выполнения |
| 0x0002 | ERR_UNKNOWN_CMD | Неизвестная команда |
| 0x0003 | ERR_INVALID_PARAMS | Некорректные параметры |
| 0x0004 | ERR_BUSY | Система занята (задание уже выполняется) |
| 0x0005 | ERR_NOT_INIT | Система не инициализирована |
| 0x10xx | Ошибки дозатора | Моторы, датчики, позиционирование |
| 0x20xx | Ошибки миксера | Моторы, коллизии |
| 0x30xx | Ошибки станции промывки | Насосы, клапаны, вода |
| 0x40xx | Ошибки ротора реагентов | Моторы, температура |
| 0x41xx | Ошибки диска образцов | Моторы, датчики |
| 0x50xx | Ошибки фотометра | Лампа, детектор, калибровка |
| 0x60xx | Ошибки реакционного диска | Моторы, температура |
| 0x70xx | Ошибки термостата | Датчики, нагреватель |

---

## 11. Состояния системы

```c
typedef enum {
    SYS_STATE_POWER_ON,       // После включения
    SYS_STATE_INITIALIZING,   // Выполняется INIT
    SYS_STATE_READY,          // Готова к работе
    SYS_STATE_ERROR           // Ошибка
} SystemState_t;
```

```
POWER_ON ──INIT──► INITIALIZING ──ok──► READY ──error──► ERROR
                        │                  ▲
                        └─────error────────┘
```

---

## 12. Структура файлов проекта

```
STM32H723_mother_board/
├── App/
│   ├── Inc/
│   │   ├── app_config.h                 # Конфигурация (очереди, тайм-ауты)
│   │   ├── app_init_checker.h           # Проверка инициализации
│   │   ├── shared_resources.h           # Extern-объявления ресурсов
│   │   ├── Dispatcher/
│   │   │   ├── command_parser.h         # Парсер + структуры команд
│   │   │   ├── command_protocol.h       # CAN-команды исполнителям
│   │   │   ├── dispatcher_io.h          # I/O ответов USB
│   │   │   ├── can_message.h            # Структура CAN-сообщения
│   │   │   ├── can_packer.h             # Упаковка в CAN-кадры
│   │   │   ├── job_manager.h            # Менеджер заданий
│   │   │   ├── recipe_store.h           # Хранилище рецептов
│   │   │   ├── parameter_parser.h       # Парсер параметров
│   │   │   ├── param_translator.h       # Трансляция параметров
│   │   │   └── direct_command_handlers.h # Прямые обработчики
│   │   └── Tasks/
│   │       ├── task_can_handler.h
│   │       ├── task_usb_handler.h
│   │       ├── task_dispatcher.h
│   │       ├── task_watchdog.h
│   │       ├── task_jobs_monitor.h
│   │       └── task_logger.h
│   └── Src/
│       ├── Dispatcher/                  # Реализации модулей
│       │   ├── command_parser.c         (366 строк)
│       │   ├── dispatcher_io.c          (208 строк)
│       │   ├── parameter_parser.c       (237 строк)
│       │   ├── param_translator.c       (281 строк)
│       │   ├── job_manager.c            (526 строк)
│       │   ├── recipe_store.c           (710 строк)
│       │   ├── can_packer.c             (50 строк)
│       │   └── direct_command_handlers.c (45 строк)
│       └── Tasks/                       # Реализации задач
│           ├── task_dispatcher.c        (169 строк)
│           ├── task_can_handler.c       (183 строк)
│           ├── task_usb_handler.c       (51 строк)
│           ├── task_watchdog.c          (26 строк)
│           ├── task_jobs_monitor.c      (32 строк)
│           ├── task_logger.c            (35 строк)
│           └── app_init_checker.c
├── Core/
│   ├── Inc/                             # HAL/RTOS конфигурация
│   │   ├── main.h
│   │   ├── FreeRTOSConfig.h
│   │   └── stm32h7xx_*.h
│   ├── Src/
│   │   ├── main.c                       (620 строк, точка входа)
│   │   ├── freertos.c
│   │   └── stm32h7xx_*.c
│   └── Startup/
│       └── startup_stm32h723zgtx.s
├── Drivers/                             # HAL + CMSIS
├── Middlewares/                          # FreeRTOS + USB Library
├── USB_DEVICE/                          # USB CDC реализация
├── App_user/                            # Python тест-сьюит
│   ├── test_combined_commands.py        # Комплексные тесты (основной)
│   ├── test_main_processes.py           # Тесты процессов
│   ├── test_analysis_cycle.py           # Цикл анализа
│   ├── test_protocol.py                 # Тесты протокола
│   ├── send_commands.py                 # Отправка команд
│   └── listen_debug.py                  # Прослушка отладки
├── readme/
│   ├── Commands_API/
│   │   ├── User_Commands/
│   │   │   ├── protocol.md              # Спецификация протокола
│   │   │   ├── commands.md              # Реестр команд
│   │   │   ├── errors.md                # Коды ошибок
│   │   │   ├── examples.md              # Примеры пакетов
│   │   │   └── full_examples.md
│   │   └── CAN_Protocol/
│   │       ├── 1_Physical_Layer.md
│   │       ├── 2_Frame_Format.md
│   │       ├── 3_Application_Layer.md
│   │       ├── 4_Examples.md
│   │       ├── 5_Low_Level_Commands.md
│   │       ├── 6_Parameter_Packing.md
│   │       └── 7_Full_CAN_Frame_Mapping.md
│   └── Report_*.md / Implementation_Plan_*.md
├── STM32H723_mother_board.ioc           # CubeMX-конфигурация
├── STM32H723ZGTX_FLASH.ld              # Линкер-скрипт (Flash)
└── STM32H723ZGTX_RAM.ld                # Линкер-скрипт (RAM)
```

---

## 13. Статистика кодовой базы

### Пользовательский код (App/)

| Категория | Файлов | Строк (прибл.) |
|-----------|--------|-----------------|
| Dispatcher (парсер, рецепты, IO) | 8 .c + 10 .h | ~3 400 |
| Tasks (задачи RTOS) | 7 .c + 6 .h | ~500 |
| Конфигурация | 3 .h | ~100 |
| **Итого App/** | **28** | **~4 000** |

### Сгенерированный код

| Категория | Назначение |
|-----------|------------|
| Core/Src/ | Инициализация MCU, HAL MSP (CubeMX) |
| USB_DEVICE/ | USB CDC (CubeMX) |
| Drivers/ | STM32H7xx HAL/LL + CMSIS |
| Middlewares/ | FreeRTOS kernel + USB library |

### Тестовый код (Python)

| Файл | Размер |
|------|--------|
| test_combined_commands.py | 28 KB (основной) |
| test_main_processes.py | 26 KB |
| test_analysis_cycle.py | 21 KB |

---

## 14. Результаты аудита

### 14.1. Реализовано и работает (11 команд)

| # | Команда | Код | Тесты |
|---|---------|-----|-------|
| 1 | GET_STATUS | 0x1000 | Пройдены |
| 2 | INIT | 0x1002 | Пройдены |
| 3 | DISPENSER_WASH | 0x2000 | Пройдены |
| 4 | DISPENSER_ASPIRATE | 0x2100 | Пройдены |
| 5 | DISPENSER_DISPENSE | 0x2200 | Пройдены |
| 6 | MIXER_MIX | 0x3100 | Пройдены |
| 7 | WASH_STATION_WASH | 0x4000 | Пройдены |
| 8 | WASH_STATION_FILL | 0x4100 | Пройдены |
| 9 | REAGENT_ROTATE | 0x5000 | Пройдены |
| 10 | SAMPLE_ROTATE | 0x5110 | Пройдены |
| 11 | PHOTOMETER_SCAN_SINGLE | 0x6100 | Пройдены |

### 14.2. Заглушки / неполные реализации

| Компонент | Состояние | Критичность |
|-----------|-----------|-------------|
| CAN Packer (can_packer.c) | Заглушка, возвращает пустые данные | **Высокая** — без него нет связи с исполнителями |
| task_watchdog | Заглушка (26 строк) | Средняя |
| task_logger | Заглушка (35 строк) | Низкая |
| Обработка CAN-ответов | Не реализована полностью | **Высокая** |
| Калибровочные константы | Все — заглушки | **Высокая** для реального железа |

### 14.3. Архитектурные замечания

1. **MAX_CONCURRENT_JOBS:** в `app_config.h` задано 5, но в `job_manager.h` жестко указано 1. Рассогласование.

2. **Отсутствует device_mapping.h:** ID моторов и насосов разбросаны по рецептам. Рекомендуется централизация.

3. **Конфликт ID мотора 3:** используется и для реакционного диска, и для Z-оси дозатора в некоторых контекстах. Требует верификации.

4. **Нет механизма обратной связи от CAN:** JobManager отправляет CAN-кадры, но полноценный прием и обработка ответов исполнителей не реализованы. Шаги рецепта завершаются по таймауту или немедленно.

5. **Отсутствует EMERGENCY_STOP (0x1010):** критически важная для безопасности команда не реализована.

6. **Нет проверки busy-состояния:** при получении второй команды во время выполнения первой — поведение может быть непредсказуемым (зависит от реализации JobManager_StartNewJob при MAX_CONCURRENT_JOBS=1).

### 14.4. Нереализованные группы команд

| Группа | Кол-во команд | Приоритет |
|--------|---------------|-----------|
| Термоконтроль (0x80xx) | 7+ | Высокий — для анализа нужна термостабилизация |
| Датчики (0x90xx) | 10+ | Средний |
| Реакционный диск (0x70xx) | 2 | Средний |
| Сканирование ШК (0x51xx) | 2 | Низкий |
| Калибровка фотометра (0x6200) | 1 | Средний |
| EMERGENCY_STOP (0x1010) | 1 | **Критический** |

---

## 15. Хронология разработки

| Дата | Событие |
|------|---------|
| 27.11.2025 | Создание проекта, app_config.h |
| 03.12.2025 | Recipe Store, Job Manager, Command Parser |
| 04.12.2025 | Dispatcher I/O, Job Manager API |
| 11.12.2025 | CAN Protocol, CAN Message |
| 22.01.2026 | Добавлен command_code в UniversalCommand_t |
| 29.01.2026 | ARGS_TYPE_PARSED, типизированные аргументы |
| 03.02.2026 | ParamSource_t — динамические параметры рецептов |
| 05.02.2026 | WASH_STATION_WASH (0x4000) |
| 06.02.2026 | param_translator.h, трансляция кювет → шаги |
| 11.02.2026 | SAMPLE_ROTATE (0x5110), DISPENSER_ASPIRATE (0x2100) |
| 13.02.2026 | DISPENSER_DISPENSE (0x2200), REAGENT_ROTATE (0x5000), MIXER_MIX (0x3100) |
| 16.02.2026 | PHOTOMETER_SCAN_SINGLE (0x6100), ACTION_PERFORM_SCAN |
| 17.02.2026 | WASH_STATION_FILL (0x4100), структурированный ERROR-ответ |

---

## 16. Пример бинарного обмена

### Команда DISPENSER_WASH (0x2000)

**Запрос:** Промыть дозатор #1, объем 100 мкл, 2 цикла

```
Hex: 43 4D 3E 00 07 20 00 01 00 64 02 47
     ─────────── ───── ───── ── ───── ── ──
     Заголовок   Длина  Cmd  ID Volume Cy CRC
     CM>         7 байт            100  2
```

**ACK-ответ:**
```
Hex: 43 4D 3E 00 06 20 00 01 00 00 21
     ─────────── ───── ───── ── ───── ──
     Заголовок   Длина  Cmd  ACK  OK  CRC
```

**DONE-ответ (после выполнения):**
```
Hex: 43 4D 3E 00 06 20 00 02 00 00 22
     ─────────── ───── ───── ── ───── ──
     Заголовок   Длина  Cmd  DONE OK  CRC
```

---

*Документ сгенерирован на основе аудита кодовой базы от 02.03.2026*
