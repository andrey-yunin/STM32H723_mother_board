import serial
import time

# --- НАСТРОЙКИ ---
# Укажите имя вашего COM-порта.
# Пример для Linux: '/dev/ttyACM0'
# Пример для Windows: 'COM3'
SERIAL_PORT = '/dev/ttyACM0' 

# Скорость порта (для USB VCP обычно не имеет значения, но должна быть указана)
BAUD_RATE = 9600

# Команды для отправки
COMMANDS = [
    "set_step_motor_start_position",
    "rotate_step_motor_clockwise",
    "rotate_step_motor_counterclockwise",
    "get_step_motor_position"
]

# Задержка между командами в секундах
DELAY_BETWEEN_COMMANDS = 1

# --- ОСНОВНОЙ СКРИПТ ---

def main():
    """
    Основная функция для подключения к порту и отправки команд.
    """
    print(f"Попытка подключения к порту {SERIAL_PORT} на скорости {BAUD_RATE}...")
    
    ser = None  # Инициализируем переменную
    try:
        # Открываем последовательный порт
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print("Соединение установлено.")
        
        # Небольшая пауза, чтобы устройство успело инициализироваться
        time.sleep(2)

        # Отправляем команды по одной
        for command in COMMANDS:
            print(f"Отправка команды: {command}")

            # ОЧИЩАЕМ ВХОДНОЙ БУФЕР ПЕРЕД ОТПРАВКОЙ                                                                                                                                                                                     
            ser.reset_input_buffer() 
            
            # Добавляем символ новой строки и кодируем в байты
            command_to_send = (command + '\n').encode('utf-8')
            
            # Отправляем данные
            ser.write(command_to_send)
            
            # Ждем ответа (опционально, но полезно для отладки)
            response = ser.readline().decode('utf-8').strip()
            if response:
                print(f"Ответ от устройства: {response}")

            # Ждем перед отправкой следующей команды
            time.sleep(DELAY_BETWEEN_COMMANDS)

        print("\nВсе команды успешно отправлены.")

    except serial.SerialException as e:
        print(f"Ошибка: Не удалось открыть порт {SERIAL_PORT}.")
        print(f"Подробности: {e}")
        print("Пожалуйста, проверьте имя порта и подключение устройства.")

    except Exception as e:
        print(f"Произошла непредвиденная ошибка: {e}")

    finally:
        # Убедимся, что порт будет закрыт, даже если произошла ошибка
        if ser and ser.is_open:
            ser.close()
            print("Соединение закрыто.")

if __name__ == "__main__":
    main()
