# План работ: Завершение реализации бинарного протокола USB CDC

## Цель этапа:
Полностью завершить переход микроконтроллера на бинарный протокол обмена данными по USB, включая прием команд, отправку всех типов ответов (ACK, DONE, ERROR) и интеграцию с JobManager для обработки команд.

## Выполненные шаги:
*   Объявление и создание FreeRTOS Stream Buffer для приема данных.
*   Модификация функции приема USB CDC (`usbd_cdc_if.c`).
*   Модификация задачи-диспетчера (`task_dispatcher.c`) для сборки бинарных пакетов.
*   Создание функции обработки бинарных команд (`command_parser.c`).
*   Создание функции отправки бинарных ответов (ACK, NACK) (`dispatcher_io.c`).
*   Корректировка `app_init_checker.c`.
*   Обновление `App_user/test_protocol.py` для отправки команды `INIT`.
*   Адаптация `task_usb_handler.c` для корректной отправки бинарных данных.
*   **Успешное тестирование базового взаимодействия с ПК**: ПК отправляет команду `INIT`, и микроконтроллер корректно отвечает бинарным ACK-пакетом, который успешно интерпретируется скриптом на ПК.
*   **Архитектурный рефакторинг: Универсальная структура команд**:
    *   **`App/Inc/Dispatcher/command_parser.h`**: Заменена `ParsedCommand_t` на `UniversalCommand_t` с `union` для строковых и бинарных аргументов.
    *   **`App/Inc/Dispatcher/job_manager.h`**: Обновлена сигнатура `JobManager_StartNewJob` для приема `UniversalCommand_t*`. Структура `JobContext_t` обновлена для хранения `UniversalCommand_t`.
    *   **`App/Src/Dispatcher/command_parser.c`**: Полностью переработан для использования `UniversalCommand_t` как для строковых, так и для бинарных команд, включая обработку параметров и вызов `JobManager_StartNewJob`.
    *   **`App/Src/Tasks/task_dispatcher.c`**: Обновлен вызов `JobManager_StartNewJob` в блоке автоматической инициализации для использования `UniversalCommand_t`.
    *   **`App/Src/Dispatcher/job_manager.c`**: Функция `JobManager_StartNewJob` адаптирована для приема `UniversalCommand_t*` и сохранения ее в контексте задания.

## Оставшиеся шаги реализации:

### 1. Расширение логики `JobManager` для обработки аргументов `UniversalCommand_t`:
*   **1.1. Доработка `JobManager_ExecuteStep`**:
    *   Адаптировать `JobManager_ExecuteStep` для использования аргументов, переданных в `UniversalCommand_t` (через `job->initial_cmd`) для параметризации действий (`AtomicAction_t`). Например, для команды `INIT` использовать `modules_mask` из `job->initial_cmd.args.binary.raw` для вызова соответствующих функций инициализации.
    *   Это потребует расширения `AtomicAction_t` для хранения динамических параметров, либо создания механизма передачи этих параметров в функции-исполнители рецептов.

### 2. Добавление функций для отправки всех типов ответов:
*   **2.1. Добавление `Dispatcher_SendDone` и `Dispatcher_SendError`**:
    *   Создать в `dispatcher_io.h` и `dispatcher_io.c` функции для отправки `DONE`- и `ERROR`-ответов согласно протоколу.
    *   Эти функции будут использоваться `JobManager` после завершения выполнения заданий.

## Зависимости:
*   FreeRTOS (Stream Buffer API, Queue API, Semaphore API).
*   Correct operation of USB CDC.
*   Наличие и корректная работа `JobManager`.

## Тестирование:
*   После каждого шага, компиляция и проверка на отсутствие ошибок.
*   Использование `App_user/test_protocol.py` для валидации бинарного обмена.
```