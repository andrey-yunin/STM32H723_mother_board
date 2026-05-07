# Отчет: подготовка рефакторинга Дирижера DDS-240

**Дата:** 5 мая 2026 г.  
**Статус:** рабочий отчет, кодовые правки не выполнялись AI-ассистентом.

## 1. Режим работы

В проекте принят режим учитель-консультант:

- ведущий инженер проекта корректирует исходный код;
- AI-ассистент объясняет архитектурные решения, предлагает локальные блоки изменений и проверяет ход рефакторинга;
- AI-ассистент самостоятельно ведет документацию, планы и отчеты;
- исходный код AI-ассистент не меняет без прямого разрешения.

## 2. Главная граница рефакторинга

Для Дирижера фиксируется цепочка ответственности:

- `command_parser` распознает команду, проверяет длину и выбирает direct handler или recipe.
- `parameter_parser` разбирает Host payload строго по `Commands_API/User_Commands`.
- `recipe/job layer` управляет сценарием и моментом расчета low-level параметров.
- `calibrator` переводит пользовательские физические величины в параметры исполнителей.
- `device_mapping` выбирает физический `NodeID/ch_idx`.
- `can_packer` формирует strict `DLC=8` CAN-команду executor-уровня.

Смысл разделения:

- Host API не смешивается с executor payload.
- Parser не принимает физических решений.
- Калибровка может позже переехать из статической таблицы во Flash/service layer без изменения Host parser.

## 2.1. Принцип общей экосистемы

Дирижер, Motion Executor, Fluidics Executor, Thermo Executor и будущие исполнители работают в одной экосистеме DDS-240.

Общие правила экосистемы обязательны для всех участников:

- CAN bitrate, 29-bit Extended ID и strict `DLC=8`;
- структура CAN ID, message types и subtypes;
- common service commands `F001..F007`;
- NACK registry и magic keys;
- endian и формат executor payload;
- `ACK -> DATA... -> DONE` ordering;
- timeout classes: ACK timeout, fast/state timeout, operation timeout, fallback timeout;
- discovery/status/UID contract.

Локальная специфика платы допускается только ниже общего контракта: физические каналы, safe limits, калибровки, home/motion profile, pump flow model, sensor conversion time, scan profile и resource groups.

Вывод для следующих блоков: timeout policy внедряется не как частная доработка Motion или Fluidics, а как общий контракт DDS-240 с параметризацией под конкретного исполнителя.

## 3. Калибратор насосов

Под калибратором в текущем этапе понимается именно перевод объема, полученного от Host, во время работы насоса:

- вход: логический насос, объем в микролитрах, тип операции;
- выход: рассчитанная длительность работы насоса в миллисекундах;
- при отсутствии валидной калибровки команда на Fluidics не отправляется.

Важное правило: `parameter_parser` не должен рассчитывать `duration_ms`. Он сохраняет `volume_ul` и `cuvette`, а расчет времени выполняется позже через `calibrator`.

## 4. Первый найденный разрыв

Команда `WASH_STATION_FILL (0x4100)` должна парситься по Host API:

- первый параметр: `volume`;
- второй параметр: `cuvette`.

Текущий код исторически использует обратный порядок:

- первый параметр трактуется как `cuvette`;
- второй параметр трактуется как `volume`.

Также текущий parser сразу рассчитывает `pump_duration_ms`, что смешивает parser и calibrator. Это нужно разделить в первом рабочем патче.

## 5. Ближайший рекомендуемый блок работ

Минимальный блок для инженера:

1. В `ParsedArgs_WashStationFill` хранить `volume_ul` и `cuvette`, не `pump_duration_ms`.
2. В `parameter_parser.c` для `0x4100` читать `volume` первым, `cuvette` вторым.
3. Создать минимальный `calibrator.h/.c`.
4. В recipe/job слое рассчитывать `duration_ms` через calibrator.
5. После этого переводить `WASH_STATION_FILL` на `PUMP_RUN_DURATION`.

## 6. Документация

План рефакторинга ведется в:

- `readme/Implementation_Plan_20260505_Conductor_Refactoring.md`

Этот отчет будет обновляться по мере принятия решений и прохождения этапов.

## 7. Журнал: parser boundary для WASH_STATION_FILL

Цель ближайшего блока: убрать физический расчет времени насоса из `parameter_parser`.

### 7.1. Принятое решение

- `ParsedArgs_WashStationFill` должен хранить Host-поля: объем и номер кюветы.
- `duration_ms` не должен храниться как результат parser-а.
- Расчет времени работы насоса выполняется отдельным calibrator-слоем.
- Расчет шагов позиционирования выполняется после parser-а, на translator/job layer.

### 7.2. Почему это важно

- Parser остается простой и проверяемой границей Host API.
- Ошибка калибровки не маскируется как ошибка разбора параметров.
- `volume -> duration` можно менять без переписывания Host parser-а.

### 7.3. Что ожидаемо изменится в коде

- В структуре разобранных аргументов `WASH_STATION_FILL` появятся только Host-поля.
- В parser-е исчезнет вызов текущей функции расчета времени насоса.
- Старые места, которые читали готовый `pump_duration_ms`, потребуют следующего шага рефакторинга.

### 7.4. Критерий завершения блока

- `parameter_parser.c` для `0x4100` больше не рассчитывает `pump_duration_ms`.
- `ParsedArgs_WashStationFill` содержит только `volume_ul` и `cuvette`.
- Смысл Host API восстановлен: первый `uint16` - объем, второй `uint16` - кювета.

## 8. Проверка блока 1

Статус: выполнено инженером, сборка проходит.

Проверено чтением кода:

- `ParsedArgs_WashStationFill` теперь хранит `volume_ul` и `cuvette`;
- descriptor `WASH_STATION_FILL` описывает порядок `volume, cuvette`;
- `parameter_parser.c` для `0x4100` больше не вызывает расчет времени насоса;
- parser boundary восстановлен.

Проверка старых полей выполнена: обращений к `wash_station_fill.pump_duration_ms` и `wash_station_fill.rotate_steps` больше нет.

## 9. Проверка блока 2

Статус: выполнено инженером, сборка проходит.

Проверено чтением кода:

- создан минимальный слой `calibrator.h/.c`;
- добавлен API перевода объема в длительность работы насоса;
- `job_manager.c` подключает `param_translator` и `calibrator`;
- позиционирование `WASH_STATION_FILL` теперь рассчитывается из `cuvette` через translator boundary;
- длительность насоса теперь рассчитывается из `volume_ul` через calibrator boundary.

Текущий блок закрывает архитектурное разделение parser/calibrator для `WASH_STATION_FILL`. Следующий блок: ввод отдельного action для `PUMP_RUN_DURATION`, чтобы уйти от старого рецепта `START_PUMP -> WAIT_MS -> STOP_PUMP`.

## 10. Контрольная точка сборки

Инженер подтвердил: после разделения `parameter_parser` и `calibrator` проект собирается.

Следующий этап можно начинать с устойчивой точки: добавить отдельное atomic action для finite-команды Fluidics `PUMP_RUN_DURATION`.

## 11. Проверка блока 3

Статус: выполнено инженером, сборка проходит.

Сделано:

- добавлен atomic action для finite-команды насоса;
- `WASH_STATION_FILL` переведен с `START_PUMP -> WAIT_MS -> STOP_PUMP` на одну команду `PUMP_RUN_DURATION`;
- старые `ACTION_START_PUMP` и `ACTION_STOP_PUMP` сохранены для service/manual flows;
- при нулевой длительности насосная CAN-команда не отправляется.

Контроль сборки:

- `text`: 80904;
- `data`: 348;
- `bss`: 46412;
- общий размер: 127664 байта.

Открытый риск: текущий общий `JOB_TIMEOUT_MS = 5000` находится на границе для примера `500 мкл * 10 мс/мкл = 5000 мс`. Для реального `PUMP_RUN_DURATION` нужен operation timeout с запасом от рассчитанной длительности.

## 12. Решение: timeout policy для всех исполнителей

Решение принято как системное, не только для Fluidics.

Дирижер должен считать operation timeout из контракта конкретной atomic-команды:

- Fluidics finite-команды ждут физическую длительность операции плюс запас;
- Motion finite-команды ждут расчетное время движения плюс запас;
- Thermo DATA-команды ждут время конверсии/чтения датчиков;
- Photometer scan-команды ждут профиль сканирования;
- быстрые state/service-команды используют короткий fast/state timeout.

Единый `JOB_TIMEOUT_MS` остается только как default/fallback, но не как промышленная модель ожидания физических операций.

## 13. Проверка блока 4

Статус: выполнено инженером, сборка проходит.

Сделано:

- у job появился per-step timeout;
- каждый новый step стартует с базового `JOB_TIMEOUT_MS`;
- `PUMP_RUN_DURATION` расширяет timeout шага на рассчитанную длительность плюс margin;
- общая проверка timeout теперь использует timeout текущего шага.

Результат: finite-команда насоса больше не ограничена жестким общим `JOB_TIMEOUT_MS = 5000`. Это закрывает риск ложного timeout для `WASH_STATION_FILL`, где рассчитанное время насоса может быть равно или больше базового timeout.

Открыто для следующих этапов:

- аналогичная timeout policy для Motion finite-команд;
- отдельные fast/state timeout для быстрых service/state-команд;
- более точная корреляция ACK/DONE по pending action.

## 14. Проверка блока 5

Статус: выполнено AI-ассистентом по прямому разрешению, без обращения к железу.

Сделано:

- `test_main_processes.py` для `WASH_STATION_FILL` переведен на Host порядок `volume, cuvette`;
- ожидание диагностического лога обновлено с `START_PUMP` на `RUN_PUMP_DURATION`;
- вызов сценария обновлен на `500 мкл, кювета 10`;
- синтаксическая проверка `python3 -m py_compile App_user/test_main_processes.py` проходит.

Закрыто позже: полный USB/CAN regression прошел на CANable fake executors, см. раздел 16.

## 15. Проверка блока 6

Статус: выполнено AI-ассистентом по прямому разрешению, сборка проекта проходит.

Сделано:

- `WASH_STATION_WASH` переведен с `START_PUMP -> WAIT_MS -> STOP_PUMP` на finite-команды `PUMP_RUN_DURATION`;
- заполнение и слив в `WASH_STATION_WASH` теперь выполняются отдельными atomic action с фиксированной длительностью 1000 ms;
- в `recipe_store.c` больше нет recipe-использований `ACTION_START_PUMP` и `ACTION_STOP_PUMP`;
- `ACTION_START_PUMP` и `ACTION_STOP_PUMP` сохранены в коде как service/manual actions;
- синтаксическая проверка Python-тестов проходит.

Закрыто позже: Fluidics finite-command smoke подтвержден на CANable fake executors, см. раздел 16. Следующие этапы относятся к Motion timeout/correlation и обновлению старых log expectations.

## 16. CANable E2E регрессия

**Дата:** 6 мая 2026 г.  
**Статус:** PASS на fake executors через CANable/SocketCAN.

Стенд:

- Дирижер STM32H723;
- USB CDC Host test `test_main_processes.py`;
- CANable `can0`, 1 Mbit/s, `ERROR-ACTIVE`;
- fake Motion `0x20` и fake Fluidics `0x30` через `can_test.py --can-only-responder`.

Подтверждено:

- discovery `SRV_GET_INFO` проходит, inventory наполняется fake Motion/Fluidics;
- `INIT` проходит через Motion `MOTOR_HOME`;
- полный `test_main_processes.py` завершился сообщением об успешном прохождении всех реализованных шагов;
- `WASH_STATION_FILL` отправляет Host payload в порядке `volume,cuvette`;
- `WASH_STATION_FILL` формирует `PUMP_RUN_DURATION` на Fluidics `0x30`, канал `10`, длительность `5000 ms`;
- `WASH_STATION_WASH` формирует два `PUMP_RUN_DURATION`: канал `10` на `1000 ms` и канал `11` на `1000 ms`;
- Host получает `DONE 0x0000` для `WASH_STATION_FILL` и `WASH_STATION_WASH`.

Предупреждения тестов по старому формату логов `ID:...` были отдельным хвостом: текущая прошивка логирует `Phys:<node>:<channel>` и `SysID`.

## 17. Чистка ожиданий логов

**Дата:** 6 мая 2026 г.  
**Статус:** выполнено AI-ассистентом по прямому разрешению.

Сделано:

- ожидания `ROTATE_MOTOR` в `test_main_processes.py` переведены с логических `ID:...` на физический формат `Phys:32:<channel>`;
- ожидание фотометра переведено с `PERFORM_SCAN (ID:...)` на текущий лог `SCAN (SysID:...)`;
- ожидания насосов оставлены в формате `RUN_PUMP_DURATION (Phys:48:...)`;
- синтаксическая проверка `python3 -m py_compile App_user/test_main_processes.py` проходит.

Ожидаемый результат: следующий CANable E2E regression должен пройти без предупреждений по старым log expectations.

## 18. Clean CANable E2E regression

**Дата:** 6 мая 2026 г.  
**Статус:** PASS.

Подтверждено на стенде:

- полный `test_main_processes.py` завершился успешным сообщением;
- предупреждений `WARNING: Log ... не найден до DONE` больше нет;
- `WASH_STATION_FILL` отправляет Host payload `volume=500`, `cuvette=10`;
- `WASH_STATION_FILL` выполняет `RUN_PUMP_DURATION (Phys:48:10, Duration:5000)`;
- `WASH_STATION_WASH` выполняет `RUN_PUMP_DURATION` по каналам `10` и `11` с длительностью `1000 ms`;
- Host получает `DONE 0x0000` по всем реализованным шагам сценария.

Итог: блок перевода насосных recipe на finite Fluidics-команды и обновления regression-теста закрыт.

## 19. Motion operation timeout

**Дата:** 6 мая 2026 г.  
**Статус:** выполнено инженером, сборка и fake CAN E2E regression проходят.

Сделано:

- для `MOTOR_ROTATE` timeout шага рассчитывается из `abs(steps) / speed` плюс margin;
- `steps != 0 && speed == 0` блокируется до отправки CAN-команды;
- для `MOTOR_HOME` используется отдельный home-profile timeout;
- timeout для `PUMP_RUN_DURATION` применяется только к валидной finite Fluidics operation;
- общий `JOB_TIMEOUT_MS` остается fallback для action без собственного operation timeout.

Проверка:

- проект собран и перезалит инженером;
- `can_test.py --can-only-responder` подтвердил штатные ACK/DONE от fake Motion `0x20` и Fluidics `0x30`;
- полный `test_main_processes.py` завершился успешным сообщением без warning;
- `WASH_STATION_FILL` и `WASH_STATION_WASH` продолжают работать через `RUN_PUMP_DURATION`;
- Motion-рецепты проходят без регрессии на fake executor.

Открытый следующий блок: `MIXER_MIX` пока показывает `WAIT_MS for 0 ms` и `START_CONTINUOUS` со скоростью `0`; это отдельная коррекция recipe/action параметров, не часть закрытого timeout-блока.

## 20. ParamSource normalization: dispenser block

**Дата:** 6 мая 2026 г.  
**Статус:** кодовый блок внесен, локальная сборка в среде AI не завершена из-за отсутствия `arm-none-eabi-gcc` в `PATH`.

Сделано:

- command-specific источники дозатора заменены на смысловые `PARAM_SOURCE_DISPENSER_*`;
- рецепты `DISPENSER_WASH`, `DISPENSER_ASPIRATE`, `DISPENSER_DISPENSE` используют общие источники `ROTATE_STEPS`, `Z_STEPS_DOWN/UP`, `SYRINGE_STEPS`;
- `JobManager` выбирает конкретные parsed-поля дозатора по `initial_recipe_id`.

Смысл блока: `ParamSource_t` начинает описывать назначение параметра внутри устройства, а не имя Host-команды, из которой этот параметр пришел.

## 21. ParamSource normalization: wash station block

**Дата:** 6 мая 2026 г.  
**Статус:** кодовый блок внесен; сборку выполняет инженер в CubeIDE/toolchain-среде.

Сделано:

- Host payload сохранен по документации: `WASH_STATION_WASH` принимает `cycles,cuvette`, `WASH_STATION_FILL` принимает `volume,cuvette`;
- `ParsedArgs_WashStationWash` теперь хранит исходный Host-параметр `cuvette`, а не только рассчитанные шаги;
- recipe использует общий источник реакционного диска `PARAM_SOURCE_REACTION_DISK_ROTATE_STEPS` и источник дозирования `PARAM_SOURCE_WASH_STATION_FILL_DURATION_MS`;
- расчет `cuvette -> steps` и `volume -> duration_ms` выполняется в `JobManager` через translator/calibrator boundary.

Смысл блока: parser не теряет Host-поля моющей станции, а recipe больше не привязан к именам конкретных команд `WASH`/`FILL`.

## 22. ParamSource normalization: reagent/sample disk block

**Дата:** 6 мая 2026 г.  
**Статус:** кодовый блок внесен; сборку выполняет инженер в CubeIDE/toolchain-среде.

Сделано:

- Host payload сохранен по документации: `SAMPLE_ROTATE` принимает `slot`, `REAGENT_ROTATE` принимает `rotor_id,slot`;
- `ParsedArgs_SampleRotate` хранит исходный Host `slot`;
- `ParsedArgs_ReagentRotate` хранит исходные Host `rotor_id` и `slot`;
- оба recipe используют общий смысловой источник `PARAM_SOURCE_REAGENT_SAMPLE_ROTATE_STEPS`;
- расчет шагов выполняется в `JobManager` через соответствующий translator для sample/reagent сценария.

Проверка по Host API:

- в разделе `0x50xx` также описаны `REAGENT_SCAN_BARCODE (0x5100)`, `REAGENT_GET_TEMP (0x5200)`, `REAGENT_SET_TEMP (0x5300)`;
- в текущем кодовом блоке они не реализовывались и не изменялись;
- эти команды требуют отдельного разбора, потому что barcode/temp сценарии связаны с DATA-ответами, а не только с поворотом мотора.

Смысл блока: общий диск образцов/реагентов остается одной физической осью, а различие Host-команд сохраняется только в parsed-полях и расчете параметра. Команды `0x5100/0x5200/0x5300` зафиксированы как отдельная будущая работа, чтобы не смешивать их с `REAGENT_ROTATE`.

## 23. Photometer command group correction

**Дата:** 6 мая 2026 г.  
**Статус:** кодовый блок внесен; сборку выполняет инженер в CubeIDE/toolchain-среде.

Сделано:

- `PHOTOMETER_SCAN_SINGLE (0x6100)` сохраняет Host payload `cuvette,wavelengths`;
- parser больше не рассчитывает шаги для фотометра;
- `WASH_STATION_WASH`, `WASH_STATION_FILL` и `PHOTOMETER_SCAN_SINGLE` используют общий `PARAM_SOURCE_REACTION_DISK_ROTATE_STEPS`;
- `JobManager` переводит `cuvette -> steps` через `ParamTranslator_CuvetteToSteps()` для реакционного диска;
- удален старый фотометрический перевод `ParamTranslator_PhotometerCuvetteToSteps()`.

Проверка по Host API:

- `PHOTOMETER_SCAN_SINGLE (0x6100)` реализован;
- `PHOTOMETER_SCAN_ALL (0x6000)`, `PHOTOMETER_CALIBRATE (0x6200)`, `PHOTOMETER_GET_WAVELENGTHS (0x6300)` описаны в документации, но в текущем кодовом блоке не реализовывались.

Смысл блока: фотометр не позиционирует кювету. Любой Host-параметр `cuvette` означает целевую кювету реакционного диска, а фотометр получает только команду измерения и маску длин волн.

## 24. Reaction disk command group precheck

**Дата:** 6 мая 2026 г.  
**Статус:** проверено; кодовую реализацию `0x7000/0x7100` не начинали.

Host API описывает:

- `REACTION_ROTATE (0x7000)`: `cuvette UINT16`, `position UINT8`;
- `REACTION_HOME (0x7100)`: без параметров.

Текущее состояние кода:

- физический мотор реакционного диска уже есть как `SYS_REACTION_DISK_MOTOR`;
- этот мотор используется внутри `WASH_STATION_WASH`, `WASH_STATION_FILL`, `PHOTOMETER_SCAN_SINGLE`;
- отдельных parsed-структур и recipe для `0x7000/0x7100` пока нет;
- таблицы смещений рабочих позиций реакционного диска пока нет.

Ключевой вывод: `REACTION_ROTATE` нельзя реализовывать как простой `cuvette -> steps`, потому что Host также передает `position`. Параметр `position` должен попасть в расчет смещения рабочей позиции: фотометр, дозатор, миксер или моющая станция.

## 25. Mixer MIX parameter correction

**Дата:** 6 мая 2026 г.  
**Статус:** кодовый блок внесен; сборку выполняет инженер в CubeIDE/toolchain-среде.

Сделано:

- `MIXER_MIX (0x3100)` сохраняет Host payload `mixer_id,cuvette,duration,wash_cycles`;
- parser больше не рассчитывает шаги XY/Z для миксера;
- recipe `MIXER_MIX` использует смысловые источники `PARAM_SOURCE_MIXER_XY_STEPS`, `PARAM_SOURCE_MIXER_Z_STEPS_DOWN`, `PARAM_SOURCE_MIXER_Z_STEPS_UP`, `PARAM_SOURCE_MIXER_PADDLE_DURATION_MS`;
- `JobManager` рассчитывает XY/Z шаги через функции `ParamTranslator_Mixer*`;
- старое имя `ParamTranslator_MixerCuvetteToRotationSteps()` заменено на `ParamTranslator_MixerCuvetteToXYSteps()`;
- удален неиспользуемый `SYS_MIXER_PADDLE_MOTOR`, чтобы лопатка не числилась шаговой осью Motion;
- удалены legacy actions `ACTION_START_MIXING_MOTOR/ACTION_STOP_MIXING_MOTOR`, которые относились к старой continuous Motion-модели;
- силовая лопатка остается finite duration-командой через `SYS_MIXER_PADDLE_LOAD`.
- regression-тест `MIXER_MIX` теперь ожидает не только XY-движение, но и `RUN_PUMP_DURATION` лопатки на Fluidics ch 12.

Проверка по Host API:

- `MIXER_MIX (0x3100)` реализован;
- `MIXER_WASH (0x3000)` и `MIXER_HOME (0x3200)` пока не подключены к dispatcher descriptors/recipes;
- для `MIXER_WASH` нужен механизм повторения `cycles`, иначе Host-параметр будет принят, но не выполнен корректно.

Проверка на стенде:

- сборка проекта выполнена инженером;
- CANable fake executors Motion `0x20` и Fluidics `0x30` отвечают ACK/DONE;
- полный `test_main_processes.py` завершился успешным сообщением;
- `MIXER_MIX` выполнил `ROTATE_MOTOR (Phys:32:5)` для XY;
- `MIXER_MIX` выполнил `ROTATE_MOTOR (Phys:32:6)` для Z down/up;
- лопатка выполнена как `RUN_PUMP_DURATION (Phys:48:12, Duration:3000)`;
- старые `START_MIXING/STOP_MIXING` в прогоне не используются.

Смысл блока: parser миксера теперь держит только то, что пришло от Host, а физические расчеты находятся на границе `JobManager/translator`, как и у реакционного диска, фотометра и моющей станции.

## 26. Handoff: состояние Дирижера перед переходом к Thermo

**Дата:** 7 мая 2026 г.  
**Статус:** текущий блок Дирижера закрыт на уровне документации, сборки инженером и CANable fake-executor регрессии.

Зафиксированное состояние:

- Motion Executor и Fluidics Executor считаются приведенными к общей экосистеме DDS-240;
- Дирижер приведен к их текущему CAN/recipe-контракту;
- полный `test_main_processes.py` проходил через `can_test.py --can-only-responder`;
- в тесте подтверждены цепочки Host -> Дирижер -> Motion/Fluidics fake executors -> DONE;
- `MIXER_MIX` использует finite `RUN_PUMP_DURATION` Fluidics ch 12 для силовой лопатки;
- старые continuous mixing actions больше не используются;
- parser-слой хранит Host payload, а физические расчеты выполняются на границе `JobManager/translator/calibrator`;
- для будущего Sensor Executor создано отдельное ТЗ и добавлен интеграционный раздел в общую экосистему.

Ограничения, которые остаются осознанно:

- Sensor Executor пока не реализован кодом;
- Thermo Executor разработан отдельно и требует следующего аудита/рефакторинга;
- реальные физические проверки Motion/Fluidics выполняются в рамках их собственных проектов и отчетов;
- команды `MIXER_WASH`, `MIXER_HOME`, `REACTION_ROTATE`, `REACTION_HOME`, расширенные группы фотометра и реагентного ротора остаются будущими работами Дирижера.

Ключевой вывод: текущую фазу рефакторинга Дирижера можно завершать. Следующий рабочий блок - аудит и приведение Thermo Executor к общей экосистеме.
