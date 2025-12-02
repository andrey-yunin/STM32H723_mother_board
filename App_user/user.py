import serial
import time

# --- НАСТРОЙКИ ---
# Укажите имя вашего COM-порта.
# Пример для Linux: '/dev/ttyACM0'
SERIAL_PORT = '/dev/ttyACM3' 

# Скорость порта (для USB VCP обычно не имеет значения, но должна быть указана)
BAUD_RATE = 9600

# Тайм аут 2 сек
TIMEOUT = 2

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
        print("Соединение установлено. Ожидание Сообщений")
        print("Для выхода нажмите Сrtl+С")
        
        # Небольшая пауза, чтобы устройство успело инициализироваться
        time.sleep(2)

        # --- Создаем блок команд для отправки ---
        commands_to_send = [
            "CMD_GET_STATUS",
            "CMD_START_MOTOR_1",
            "CMD_READ_SENSOR_3",
            "CMD_SET_POWER_LEVEL_85",
            "CMD_PERFORM_DIAGNOSTICS"
            ]

        # --- Быстро отправляем все команды ---
        print("\n--- Отправка блока команд ---")
        for cmd in commands_to_send:
            print(f"Отправка: {cmd}")
            ser.write((cmd + '\n').encode('utf-8'))
            time.sleep(0.05) # Маленькая пауза, чтобы не перегружать драйвер, но все равно очень быстро


        # --- Получаем все ответы ---
        
        print("\n--- Получение ответов ---")
        responses_received = 0
        while responses_received < len(commands_to_send):
            response = ser.readline().decode('utf-8').strip()
            if response:
                print(f"Получено: {response}")
                responses_received += 1
                time.sleep(1)
                
            else:
                # Если мы не получили ответ за TIMEOUT секунд, выходим
                print(f"Таймаут ожидания ответа №{responses_received + 1}. Прерывание.")
                break

    except serial.SerialException as e:
        print(f"Ошибка: Не удалось открыть порт {SERIAL_PORT}.")
        print(f"Подробности: {e}")
        print("Пожалуйста, проверьте имя порта и подключение устройства.")

          
    except KeyboardInterrupt:
        # Обработка нажатия Ctrl+C
        print("\nПрограмма завершена пользователем.")


    except Exception as e:
        print(f"Произошла непредвиденная ошибка: {e}")

    finally:
        # Убедимся, что порт будет закрыт, даже если произошла ошибка
        if ser and ser.is_open:
            ser.close()
            print("Соединение закрыто.")

if __name__ == "__main__":
    main()
