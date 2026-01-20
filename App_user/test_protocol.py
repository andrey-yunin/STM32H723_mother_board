import serial
import time
import sys

# --- НАСТРОЙКИ ---
SERIAL_PORT = '/dev/ttyACM4' 
BAUD_RATE = 9600

def calculate_crc(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
    return crc

def build_command(command_code: int, params: bytes = b'') -> bytes:
    header = b'CM>'
    command_bytes = command_code.to_bytes(2, 'big')
    length = len(command_bytes) + len(params) + 1
    length_bytes = length.to_bytes(2, 'big')
    crc_payload = command_bytes + params
    crc = calculate_crc(crc_payload).to_bytes(1, 'big')
    packet = header + length_bytes + crc_payload + crc
    return packet

def main():
    # Получаем маску из аргументов командной строки, по умолчанию 0x01
    if len(sys.argv) > 1:
        try:
            mask = int(sys.argv[1], 16)
        except ValueError:
            print(f"Ошибка: Неверный формат маски '{sys.argv[1]}'. Используйте hex, например, 0x01 или 1F.")
            return
    else:
        mask = 0x01 # Маска по умолчанию

    print(f"Попытка подключения к порту {SERIAL_PORT}...")
    ser = None
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1) # Таймаут чтения 1 секунда
        print("Соединение установлено.")
        time.sleep(2)

        init_command_code = 0x1002
        init_params = mask.to_bytes(1, 'big')
        command_to_send = build_command(init_command_code, init_params)
        
        print(f"Отправка команды INIT с маской 0x{mask:02x}: {' '.join(f'{b:02x}' for b in command_to_send)}")

        ser.reset_input_buffer()
        ser.write(command_to_send)
        
        # --- Чтение ACK ответа ---
        response_header = ser.read(3)
        if response_header == b'CM>':
            print(f"Получен заголовок ответа: {response_header.hex(' ')}")
            response_length_bytes = ser.read(2)
            response_length = int.from_bytes(response_length_bytes, 'big')
            response_body = ser.read(response_length)
            full_response = response_header + response_length_bytes + response_body
            print(f"Полный ответ от устройства (ACK): {full_response.hex(' ')}")
        else:
            print(f"Ошибка: Не получен корректный заголовок ACK. Получено: {response_header.hex(' ')}")
            return

        # --- НОВЫЙ БЛОК: Прослушивание асинхронных сообщений ---
        print("\n--- Прослушивание отладочных сообщений от устройства (5 секунд) ---")
        start_time = time.time()
        while time.time() - start_time < 5:
            response_line = ser.readline()
            if response_line:
                try:
                    # Пытаемся декодировать как строку
                    print(f"DEVICE: {response_line.decode('utf-8').strip()}")
                except UnicodeDecodeError:
                    # Если не вышло - печатаем как hex
                    print(f"DEVICE (RAW): {response_line.hex(' ')}")
        
        print("--- Прослушивание завершено ---\
")

    except serial.SerialException as e:
        print(f"Ошибка: Не удалось открыть порт {SERIAL_PORT}. {e}")
    except Exception as e:
        print(f"Произошла непредвиденная ошибка: {e}")
    finally:
        if ser and ser.is_open:
            ser.close()
            print("Соединение закрыто.")

if __name__ == "__main__":
    main()
