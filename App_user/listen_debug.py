import serial
import time
import sys

# --- НАСТРОЙКИ ---
SERIAL_PORT = '/dev/ttyACM1' 
BAUD_RATE = 9600

def listen_mode():
    print(f"Режим прослушивания порта {SERIAL_PORT}...")
    ser = None
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print("Соединение установлено. Прослушивание 10 секунд...")
        
        start_time = time.time()
        while time.time() - start_time < 10:
            line = ser.readline()
            if line:
                try:
                    print(f"DEVICE: {line.decode('utf-8').strip()}")
                except UnicodeDecodeError:
                    print(f"DEVICE (RAW): {line.hex(' ')}")
        
        print("Прослушивание завершено.")

    except serial.SerialException as e:
        print(f"Ошибка: Не удалось открыть порт {SERIAL_PORT}. {e}")
    except Exception as e:
        print(f"Произошла непредвиденная ошибка: {e}")
    finally:
        if ser and ser.is_open:
            ser.close()
            print("Соединение закрыто.")

if __name__ == "__main__":
    listen_mode()
