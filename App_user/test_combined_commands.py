import serial
import time
import sys
import threading
import queue

# --- НАСТРОЙКИ ---
SERIAL_PORT = '/dev/ttyACM1' 
BAUD_RATE = 9600
RESPONSE_TIMEOUT = 5  # Таймаут ожидания конкретного ответа (секунды)
LISTEN_DURATION = 5   # Продолжительность прослушивания асинхронных сообщений (секунды)

# --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ДЛЯ КОММУНИКАЦИИ ---
# Очередь для хранения всех полученных сообщений (бинарных и текстовых)
received_messages_queue = queue.Queue()
# Флаг для остановки потока прослушивания
stop_listening_event = threading.Event()
# Серийный порт
ser = None

# --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ПРОТОКОЛА ---
def calculate_crc(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
    return crc

def build_command(command_code: int, params: bytes = b'') -> bytes:
    header = b'CM>'
    command_bytes = command_code.to_bytes(2, 'big')
    length = len(command_bytes) + len(params) + 1  # Cmd (2) + Params (X) + CRC (1)
    length_bytes = length.to_bytes(2, 'big')
    crc_payload = command_bytes + params
    crc = calculate_crc(crc_payload).to_bytes(1, 'big')
    packet = header + length_bytes + crc_payload + crc
    return packet

def parse_response_packet(raw_data: bytes):
    if len(raw_data) < 8 or not raw_data.startswith(b'CM>'):
        return None, raw_data # Неполный или некорректный пакет, возвращаем остаток

    try:
        payload_len = int.from_bytes(raw_data[3:5], 'big')
        
        # Проверяем, достаточно ли данных для полного пакета
        if len(raw_data) < 5 + payload_len:
            return None, raw_data # Ждем больше данных

        packet_full_payload = raw_data[5 : 5 + payload_len] # This includes CmdCode, Type/Data, CRC
        
        command_code = int.from_bytes(packet_full_payload[0:2], 'big')
        
        response_type = None
        status_or_data = b''

        # Determine packet type based on payload_len
        if payload_len == 6: # Fixed length for ACK/NACK/DONE/ERROR
            response_type = packet_full_payload[2] # Type byte
            status_or_data = packet_full_payload[3:-1] # Status bytes
        elif payload_len > 6: # Variable length for DATA
            # For DATA packets, packet_full_payload[2] is the first byte of actual data
            # There is no explicit 'response_type' byte for DATA.
            # We can assign a pseudo-type for internal handling if needed, e.g., 0x03
            response_type = 0x03 # Pseudo-type for DATA, used in Python for filtering
            status_or_data = packet_full_payload[2:-1] # Actual data bytes
        else: # Handle unexpected payload lengths
            print(f"WARNING: Unexpected payload length {payload_len} for command 0x{command_code:04x}.")
            # We can still return the parsed bits but mark as unknown type
            response_type = 0xFF # Unknown type
            status_or_data = packet_full_payload[2:-1] # Fallback to treat as data
        
        received_crc = packet_full_payload[-1]
        
        calculated_crc = calculate_crc(packet_full_payload[:-1]) # CRC для всего, кроме последнего байта (который есть CRC)

        if calculated_crc != received_crc:
            print(f"ERROR: CRC mismatch in received packet for command 0x{command_code:04x}. Raw: {raw_data.hex(' ')}")
            return None, raw_data # Ошибка CRC, возможно, некорректный пакет
        
        response_info = {
            "header": raw_data[0:3],
            "total_length": 5 + payload_len,
            "command_code": command_code,
            "response_type": response_type, # This will now be correct for fixed types or 0x03 for DATA
            "status_or_data": status_or_data,
            "crc": received_crc,
            "raw_packet": raw_data[ : 5 + payload_len]
        }
        
        remaining_data = raw_data[5 + payload_len:]
        return response_info, remaining_data
        
    except Exception as e:
        print(f"Error parsing response: {e}, Raw data: {raw_data.hex(' ')}")
        return None, raw_data # Ошибка парсинга

# --- ПОТОК ПРОСЛУШИВАНИЯ СЕРИЙНОГО ПОРТА ---
def listen_serial_port():
    global ser
    if not ser or not ser.is_open:
        print("ERROR: Serial port not open in listener thread.")
        return

    buffer = b''
    while not stop_listening_event.is_set():
        try:
            # Читаем байты по одному, чтобы не блокировать слишком долго
            byte = ser.read(1)
            if byte:
                buffer += byte
                # Пытаемся распарсить все возможные сообщения из буфера
                while True:
                    # Сначала ищем текстовые сообщения (обычно заканчиваются \n или \r\n)
                    # Новая логика: если видим CM>, то это начало бинарного пакета.
                    # Обрабатываем все до CM> как текстовое, если оно есть.
                    cm_index = buffer.find(b'CM>')
                    if cm_index != -1:
                        if cm_index > 0:
                            # Есть текст перед CM>
                            text_message = buffer[:cm_index].decode('utf-8', errors='ignore').strip()
                            if text_message:
                                received_messages_queue.put({"type": "text", "content": text_message})
                            buffer = buffer[cm_index:] # Оставляем CM> и все, что после него
                        
                        # Теперь пытаемся распарсить бинарный пакет
                        response_info, remaining_data = parse_response_packet(buffer)
                        if response_info:
                            received_messages_queue.put({"type": "binary", "content": response_info})


                            buffer = remaining_data
                        else:
                            # Недостаточно данных для бинарного пакета, ждем дальше
                            break # Выходим из внутреннего while, чтобы прочитать еще байт
                    else:
                        # Если CM> не найден, то весь буфер может быть текстовым
                        # Ищем переносы строки, чтобы выделить полные текстовые сообщения
                        newline_index = buffer.find(b'\n')
                        if newline_index != -1:
                            text_message = buffer[:newline_index].decode('utf-8', errors='ignore').strip()
                            if text_message:
                                received_messages_queue.put({"type": "text", "content": text_message})
                            buffer = buffer[newline_index+1:]
                        else:
                            # Нет CM> и нет переноса строки, ждем больше данных
                            break # Выходим из внутреннего while
            else:
                # Если ничего не прочитали, даем ЦПУ немного времени и продолжаем
                time.sleep(0.01)

        except serial.SerialException as e:
            print(f"ERROR in listener thread: {e}")
            break
        except Exception as e:
            print(f"UNEXPECTED ERROR in listener thread: {e}")
            break

    print("Listener thread stopped.")

# --- ОСНОВНЫЕ ФУНКЦИИ ТЕСТИРОВАНИЯ ---
def send_and_wait_ack(command_code: int, params: bytes = b'', expected_ack_type: int = 0x01) -> bool:
    global ser
    command_packet = build_command(command_code, params)
    print(f"Отправка команды 0x{command_code:04x}: {' '.join(f'{b:02x}' for b in command_packet)}")
    
    ser.write(command_packet)
    
    start_time = time.time()
    while time.time() - start_time < RESPONSE_TIMEOUT:
        try:
            msg = received_messages_queue.get(timeout=0.1)
            if msg["type"] == "binary" and \
               msg["content"]["command_code"] == command_code and \
               msg["content"]["response_type"] == expected_ack_type:
                print(f"Получен ACK/NACK для 0x{command_code:04x}. Ответ: {msg['content']['raw_packet'].hex(' ')}")
                return True
            else:
                # Если это не ожидаемый ACK/NACK, просто печатаем и продолжаем ждать
                if msg["type"] == "text":
                    print(f"DEVICE: {msg['content']}")
                elif msg["type"] == "binary":
                    print(f"DEVICE (BIN): {msg['content']['raw_packet'].hex(' ')}")
        except queue.Empty:
            continue
    print(f"ERROR: Таймаут ожидания ACK/NACK для команды 0x{command_code:04x}.")
    return False

def wait_for_done(command_code: int, expected_status: int = 0x0000) -> bool:
    global ser
    print(f"Ожидание DONE для команды 0x{command_code:04x}...")
    start_time = time.time()
    while time.time() - start_time < RESPONSE_TIMEOUT * 2: # Увеличиваем таймаут для DONE
        try:
            msg = received_messages_queue.get(timeout=0.1)
            if msg["type"] == "binary" and \
               msg["content"]["command_code"] == command_code and \
               msg["content"]["response_type"] == 0x02: # 0x02 - это тип DONE
                
                done_status = int.from_bytes(msg["content"]["status_or_data"], 'big')
                if done_status == expected_status:
                    print(f"Получен DONE для 0x{command_code:04x} со статусом 0x{done_status:04x}. Ответ: {msg['content']['raw_packet'].hex(' ')}")
                    return True
                else:
                    print(f"ERROR: Получен DONE для 0x{command_code:04x} со статусом 0x{done_status:04x}, ожидался 0x{expected_status:04x}.")
                    return False
            else:
                if msg["type"] == "text":
                    print(f"DEVICE: {msg['content']}")
                elif msg["type"] == "binary":
                    print(f"DEVICE (BIN): {msg['content']['raw_packet'].hex(' ')}")
        except queue.Empty:
            continue
    print(f"ERROR: Таймаут ожидания DONE для команды 0x{command_code:04x}.")
    return False

def wait_for_data_and_done(command_code: int, expected_data_len: int, expected_done_status: int = 0x0000) -> (bool, bytes):
    global ser
    print(f"Ожидание DATA и DONE для команды 0x{command_code:04x}...")
    data_received = None
    done_received = False
    
    start_time = time.time()
    while time.time() - start_time < RESPONSE_TIMEOUT * 2:
        try:
            msg = received_messages_queue.get(timeout=0.1)
            if msg["type"] == "binary":
                if msg["content"]["command_code"] == command_code:
                    if msg["content"]["response_type"] == 0x03: # 0x03 - это тип DATA
                        # Extract only the actual data part, skipping Type (1 byte) and Status (2 bytes)
                        # So, actual data starts at index 3 of status_or_data
                        actual_data_from_packet = msg["content"]["status_or_data"][3:] 
                        
                        if len(actual_data_from_packet) == expected_data_len: 
                            data_received = actual_data_from_packet
                            # Also extract the embedded status for logging/validation if needed
                            embedded_response_type = msg["content"]["status_or_data"][0]
                            embedded_status = int.from_bytes(msg["content"]["status_or_data"][1:3], 'big')
                            print(f"DEBUG: DATA packet embedded Type: 0x{embedded_response_type:02x}, Status: 0x{embedded_status:04x}")
                            print(f"Получен DATA для 0x{command_code:04x}. Данные: {data_received.hex(' ')}")
                        else:
                            print(f"WARNING: Получен DATA для 0x{command_code:04x} с некорректной длиной: {len(actual_data_from_packet)}, ожидалось {expected_data_len}.")
                    elif msg["content"]["response_type"] == 0x02: # 0x02 - это тип DONE
                        done_status = int.from_bytes(msg["content"]["status_or_data"], 'big')
                        if done_status == expected_done_status:
                            done_received = True
                            print(f"Получен DONE для 0x{command_code:04x} со статусом 0x{done_status:04x}. Raw: {{msg['content']['raw_packet'].hex(' ')}}")
                        else:
                            print(f"ERROR: Получен DONE для 0x{command_code:04x} со статусом 0x{done_status:04x}, ожидался 0x{expected_done_status:04x}.")
                            return False, None
            else: # Текстовые сообщения
                print(f"DEVICE: {msg['content']}")
            
            if data_received is not None and done_received:
                return True, data_received
                
        except queue.Empty:
            continue
            
    print(f"ERROR: Таймаут ожидания DATA и/или DONE для команды 0x{command_code:04x}.")
    return False, None

# --- ТЕСТОВЫЕ СЦЕНАРИИ ---
def test_init_command(mask: int):
    print("\n=== Тест команды INIT ===")
    if not send_and_wait_ack(0x1002, mask.to_bytes(1, 'big')):
        return False
    if not wait_for_done(0x1002):
        return False
    return True

def test_get_status_command():
    print("\n=== Тест команды GET_STATUS ===")
    if not send_and_wait_ack(0x1000):
        return False
    
    success, data = wait_for_data_and_done(0x1000, expected_data_len=3)
    if not success:
        return False
    
    if data:
        system_state = data[0]
        error_code = int.from_bytes(data[1:3], 'big')
        print(f"GET_STATUS: System State = 0x{system_state:02x}, Last Error = 0x{error_code:04x}")
        # Дополнительные проверки состояния могут быть добавлены здесь
        return True
    return False

def test_dispenser_wash_command(dispenser_id: int, volume: int, cycles: int):
    print(f"\n=== Тест команды DISPENSER_WASH (0x2000) для дозатора {dispenser_id}, объем {volume} мкл, циклов {cycles} ===")
    
    # Параметры: dispenser_id (UINT8), volume (UINT16), cycles (UINT8)
    params = dispenser_id.to_bytes(1, 'big') + \
             volume.to_bytes(2, 'big') + \
             cycles.to_bytes(1, 'big')
    
    if not send_and_wait_ack(0x2000, params):
        return False
    
    if not wait_for_done(0x2000):
        return False
    
    print(f"=== Тест DISPENSER_WASH для дозатора {dispenser_id} пройден успешно ===")
    return True

def test_combined_scenario():
    print("\n=== Комбинированный сценарий: INIT + GET_STATUS ===")
    # Отправляем INIT
    if not test_init_command(0x01):
        print("Комбинированный сценарий: INIT провален.")
        return False
    time.sleep(1) # Небольшая пауза

    # Отправляем GET_STATUS
    if not test_get_status_command():
        print("Комбинированный сценарий: GET_STATUS провален.")
        return False
    time.sleep(1)

    # Повторяем, возможно, с другим параметром INIT
    if not test_init_command(0x02):
        print("Комбинированный сценарий: INIT (второй раз) провален.")
        return False
    time.sleep(1)

    if not test_get_status_command():
        print("Комбинированный сценарий: GET_STATUS (второй раз) провален.")
        return False
    
    print("\n=== Комбинированный сценарий ЗАВЕРШЕН УСПЕШНО ===")
    return True


# --- ГЛАВНАЯ ФУНКЦИЯ ---
def main():
    global ser
    mask = 0x01 # Маска по умолчанию, если не указана
    if len(sys.argv) > 1:
        try:
            mask = int(sys.argv[1], 16)
        except ValueError:
            print(f"Ошибка: Неверный формат маски '{sys.argv[1]}'. Используйте hex, например, 0x01 или 1F.")
            return

    print(f"Попытка подключения к порту {SERIAL_PORT}...")
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0) # Таймаут чтения 0, чтобы read() не блокировал
        print("Соединение установлено.")
        time.sleep(2) # Даем устройству время на инициализацию

        ser.reset_input_buffer()
        ser.reset_output_buffer()
        
        # Запускаем поток для прослушивания порта
        listener_thread = threading.Thread(target=listen_serial_port)
        listener_thread.start()
        print("Запущен поток прослушивания.")

        # Очищаем очередь на случай, если там что-то осталось от запуска
        while not received_messages_queue.empty():
            received_messages_queue.get_nowait()

        # --- ЗАПУСК ТЕСТОВЫХ СЦЕНАРИЕВ ---
        all_tests_passed = True
        
        # Запускаем индивидуальный тест INIT для проверки
        if not test_init_command(mask):
             all_tests_passed = False

        # Запускаем индивидуальный тест GET_STATUS для проверки
        if all_tests_passed and not test_get_status_command():
            all_tests_passed = False

        # Запускаем индивидуальный тест DISPENSER_WASH для проверки
        # Пример: дозатор 1, 1000 мкл, 2 цикла
        if all_tests_passed and not test_dispenser_wash_command(1, 1000, 2):
            all_tests_passed = False

        # Запускаем комбинированный сценарий
        if all_tests_passed and not test_combined_scenario():
            all_tests_passed = False

        if all_tests_passed:
            print("\nВСЕ ТЕСТЫ ПРОЙДЕНЫ УСПЕШНО!")
        else:
            print("\nНЕКОТОРЫЕ ТЕСТЫ ПРОВАЛЕНЫ!")

    except serial.SerialException as e:
        print(f"Ошибка: Не удалось открыть порт {SERIAL_PORT}. {e}")
    except Exception as e:
        print(f"Произошла непредвиденная ошибка: {e}")
    finally:
        # Останавливаем поток прослушивания
        stop_listening_event.set()
        if 'listener_thread' in locals() and listener_thread.is_alive():
            listener_thread.join(timeout=2) # Ждем завершения потока
            if listener_thread.is_alive():
                print("WARNING: Listener thread did not stop gracefully.")
        
        if ser and ser.is_open:
            ser.close()
            print("Соединение закрыто.")

if __name__ == "__main__":
    main()
