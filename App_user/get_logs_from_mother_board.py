import serial
import time

# --- НАСТРОЙКИ ---
# Укажите имя вашего COM-порта.
# Пример для Linux: '/dev/ttyACM0'
# Пример для Windows: 'COM3'
SERIAL_PORT = '/dev/ttyACM0' 

# Скорость порта (для USB VCP обычно не имеет значения, но должна быть указана)
BAUD_RATE = 9600


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

        #Бесконечный цикл для прослушивания порта
        while True:
            # Читаем строку из порта
            # timeout = 1 означает, что ser.readline() будет ждать одной секунды.
            # Если ничего не придет, вернется пкстая строка.
            line = ser.readline().decode('utf-8').strip()

            # Если строка не пустая, печатаем ее
            print(f"Получено: {line}")   

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
