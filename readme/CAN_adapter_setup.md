# Настройка CANable адаптера

## Адаптер: CANable (прошивка candleLight)

Подключение по USB, определяется как нативный socketCAN интерфейс `can0`.

## Установка утилит

```bash
sudo apt install can-utils
```

## Проверка подключения

```bash
ip link show type can
```

Ожидаемый вывод — интерфейс `can0` в состоянии `DOWN`:

```
can0: <NOARP,ECHO> mtu 16 qdisc noop state DOWN mode DEFAULT group default qlen 10
```

## Настройка и запуск интерфейса

```bash
# Установить bitrate (1 Мбит/с — совпадает с настройкой STM32H723)
sudo ip link set can0 type can bitrate 1000000

# Поднять интерфейс
sudo ip link set can0 up
```

Проверка — должно быть `state UP`:

```bash
ip link show can0
```

## Базовые команды

```bash
# Мониторинг входящих фреймов
candump can0

# Отправка фрейма (ID=0x123, данные=DEADBEEF)
cansend can0 123#DEADBEEF

# Генерация случайного трафика
cangen can0

# Фильтрация по ID
candump can0,123:7FF
```

## Выключение интерфейса

```bash
sudo ip link set can0 down
```

## Виртуальный CAN (без физического адаптера)

Для отладки логики без оборудования:

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set vcan0 up
```

Использование аналогично — `candump vcan0`, `cansend vcan0 123#DEADBEEF`.

## Физическое подключение к STM32H723

Требуется CAN-трансивер на стороне платы (например SN65HVD230).
Прямое подключение CAN_TX/RX микроконтроллера к адаптеру невозможно — разные электрические уровни.

```
STM32H723          Трансивер            CANable
 CAN_TX ──────────► TXD   CANH ─────── CANH
 CAN_RX ◄────────── RXD   CANL ─────── CANL
 3.3V   ──────────► VCC   GND  ─────── GND
 GND    ──────────► GND

                    CANH ──[120 Ом]── CANL  (терминация)
```
