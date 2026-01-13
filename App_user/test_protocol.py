import serial
import time

# --- НАСТРОЙКИ ---
# Укажите имя вашего COM-порта.
# Пример для Linux: '/dev/ttyACM0'
# Пример для Windows: 'COM3'
SERIAL_PORT = '/dev/ttyACM0' 

# Скорость порта (для USB VCP обычно не имеет значения, но должна быть указана)
BAUD_RATE = 9600

def calculate_crc(data: bytes) -> int:
    """Вычисляет CRC как XOR всех байтов."""
    crc = 0
    for byte in data:
        crc ^= byte
    return crc

def build_command(command_code: int, params: bytes = b'') -> bytes:
    """
    Собирает пакет команды в бинарном формате.
    """
    header = b'CM>'  # 0x43 0x4D 0x3E
    
    # Команда (2 байта, Big-endian)
    command_bytes = command_code.to_bytes(2, 'big')
    
    # Длина = Длина команды + Длина параметров + Длина CRC (1)
    length = len(command_bytes) + len(params) + 1
    length_bytes = length.to_bytes(2, 'big')
    
    # Тело пакета для расчета CRC
    crc_payload = command_bytes + params
    
    # CRC (1 байт)
    crc = calculate_crc(crc_payload).to_bytes(1, 'big')
    
    # Собираем полный пакет
    packet = header + length_bytes + crc_payload + crc
    
    return packet

def main():
    """
    Основная функция для подключения к порту и отправки бинарной команды.
    """
    print(f"Попытка подключения к порту {SERIAL_PORT} на скорости {BAUD_RATE}...")
    
    ser = None
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2)
        print("Соединение установлено.")
        time.sleep(2)

        # --- Сборка и отправка команды INIT (0x1002) ---
        init_command_code = 0x1002
        init_params = b'\xff' # Инициализировать все модули
        command_to_send = build_command(init_command_code, init_params)
        
        # Ожидаемый пакет: 43 4d 3e 00 04 10 02 ff ed
        print(f"Отправка команды INIT: {' '.join(f'{b:02x}' for b in command_to_send)}")

        ser.reset_input_buffer()
        ser.write(command_to_send)
        
        # --- Чтение ответа ---
        # Ожидаемый ответ ACK: Шапка(3) + Длина(2) + Команда(2) + Тип(1) + Статус(2) + CRC(1) = 11 байт
        # Но сначала прочитаем заголовок и длину, чтобы узнать, сколько еще читать
        
        response_header = ser.read(3)
        if not response_header:
            print("Ошибка: Таймаут ожидания ответа. Устройство не ответило.")
            return

        if response_header != b'CM>':
            print(f"Ошибка: Неверный заголовок ответа: {response_header.hex(' ')}")
            # Попробуем прочитать и вывести остаток данных в буфере для диагностики
            remaining_data = ser.read(ser.in_waiting)
            print(f"Остаток в буфере: {remaining_data.hex(' ')}")
            return
            
        print(f"Получен заголовок ответа: {response_header.hex(' ')}")
        
        response_length_bytes = ser.read(2)
        if len(response_length_bytes) < 2:
            print("Ошибка: Не удалось прочитать длину ответа.")
            return
            
        response_length = int.from_bytes(response_length_bytes, 'big')
        print(f"Заявленная длина остатка пакета: {response_length} байт")
        
        # Читаем остаток пакета
        response_body = ser.read(response_length)
        
        full_response = response_header + response_length_bytes + response_body
        print(f"Полный ответ от устройства ({len(full_response)} байт): {full_response.hex(' ')}")

        # --- Проверка ответа (базовая) ---
        # В данном тесте мы просто ожидаем любой корректный ответ.
        # В реальном приложении здесь будет полная распаковка и проверка CRC.
        if len(response_body) == response_length:
            print("\nОтвет получен, проверка целостности пакета пройдена (длина совпадает).")
            print("Тест базового прохождения команды можно считать успешным.")
        else:
            print(f"\nОшибка: Длина полученного тела ответа ({len(response_body)}) не совпадает с заявленной ({response_length}).")


    except serial.SerialException as e:
        print(f"Ошибка: Не удалось открыть порт {SERIAL_PORT}.")
        print(f"Подробности: {e}")
        print("Пожалуйста, проверьте имя порта и подключение устройства.")

    except Exception as e:
        print(f"Произошла непредвиденная ошибка: {e}")

    finally:
        if ser and ser.is_open:
            ser.close()
            print("Соединение закрыто.")

if __name__ == "__main__":
    main()
