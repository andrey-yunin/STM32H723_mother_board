import serial
import time
import struct
import sys
import threading
import queue

# --- НАСТРОЙКИ ---
SERIAL_PORT = '/dev/ttyACM4'
BAUD_RATE = 9600
RESPONSE_TIMEOUT = 5  # Таймаут ожидания конкретного ответа (секунды)

# --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ДЛЯ КОММУНИКАЦИИ ---
received_messages_queue = queue.Queue()
stop_listening_event = threading.Event()
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
    length = len(command_bytes) + len(params) + 1
    length_bytes = length.to_bytes(2, 'big')
    crc_payload = command_bytes + params
    crc = calculate_crc(crc_payload).to_bytes(1, 'big')
    packet = header + length_bytes + crc_payload + crc
    return packet

def parse_response_packet(raw_data: bytes):
    if len(raw_data) < 8 or not raw_data.startswith(b'CM>'):
        return None, raw_data

    try:
        payload_len = int.from_bytes(raw_data[3:5], 'big')

        if len(raw_data) < 5 + payload_len:
            return None, raw_data

        packet_full_payload = raw_data[5 : 5 + payload_len]
        command_code = int.from_bytes(packet_full_payload[0:2], 'big')
        response_type = None
        status_or_data = b''

        if payload_len == 6:
            response_type = packet_full_payload[2]
            status_or_data = packet_full_payload[3:-1]
        elif payload_len > 6:
            response_type = 0x03
            status_or_data = packet_full_payload[2:-1]
        else:
            print(f"WARNING: Unexpected payload length {payload_len} for command 0x{command_code:04x}.")
            response_type = 0xFF
            status_or_data = packet_full_payload[2:-1]

        received_crc = packet_full_payload[-1]
        calculated_crc = calculate_crc(packet_full_payload[:-1])

        if calculated_crc != received_crc:
            print(f"ERROR: CRC mismatch in received packet for command 0x{command_code:04x}. Raw: {raw_data.hex(' ')}")
            return None, raw_data

        response_info = {
            "command_code": command_code,
            "response_type": response_type,
            "status_or_data": status_or_data,
            "raw_packet": raw_data[ : 5 + payload_len]
        }

        remaining_data = raw_data[5 + payload_len:]
        return response_info, remaining_data

    except Exception as e:
        print(f"Error parsing response: {e}, Raw data: {raw_data.hex(' ')}")
        return None, raw_data

# --- ПОТОК ПРОСЛУШИВАНИЯ СЕРИЙНОГО ПОРТА ---
def listen_serial_port():
    global ser
    if not ser or not ser.is_open:
        print("ERROR: Serial port not open in listener thread.")
        return

    buffer = b''
    while not stop_listening_event.is_set():
        try:
            byte = ser.read(1)
            if byte:
                buffer += byte
                while True:
                    cm_index = buffer.find(b'CM>')
                    if cm_index != -1:
                        if cm_index > 0:
                            text_message = buffer[:cm_index].decode('utf-8', errors='ignore').strip()
                            if text_message:
                                received_messages_queue.put({"type": "text", "content": text_message})
                            buffer = buffer[cm_index:]

                        response_info, remaining_data = parse_response_packet(buffer)
                        if response_info:
                            received_messages_queue.put({"type": "binary", "content": response_info})
                            buffer = remaining_data
                        else:
                            break
                    else:
                        newline_index = buffer.find(b'\n')
                        if newline_index != -1:
                            text_message = buffer[:newline_index].decode('utf-8', errors='ignore').strip()
                            if text_message:
                                received_messages_queue.put({"type": "text", "content": text_message})
                            buffer = buffer[newline_index+1:]
                        else:
                            break
            else:
                time.sleep(0.01)
        except Exception as e:
            print(f"ERROR in listener thread: {e}")
            break
    print("Listener thread stopped.")

# --- ОСНОВНЫЕ ФУНКЦИИ ТЕСТИРОВАНИЯ ---
def send_and_wait_ack(command_code: int, params: bytes = b'') -> bool:
    global ser
    command_packet = build_command(command_code, params)
    print(f"Отправка команды 0x{command_code:04x}: {' '.join(f'{b:02x}' for b in command_packet)}")
    ser.write(command_packet)

    start_time = time.time()
    while time.time() - start_time < RESPONSE_TIMEOUT:
        try:
            msg = received_messages_queue.get(timeout=0.1)
            if msg["type"] == "binary" and msg["content"]["command_code"] == command_code and msg["content"]["response_type"] == 0x01:
                print(f"Получен ACK/NACK для 0x{command_code:04x}. Ответ: {msg['content']['raw_packet'].hex(' ')}")
                return True
            else:
                if msg["type"] == "text":
                    print(f"DEVICE: {msg['content']}")
                elif msg["type"] == "binary":
                    print(f"DEVICE (BIN): {msg['content']['raw_packet'].hex(' ')}")
        except queue.Empty:
            continue
    print(f"ERROR: Таймаут ожидания ACK/NACK для команды 0x{command_code:04x}.")
    return False

def wait_for_done(command_code: int, expected_status: int = 0x0000) -> bool:
    print(f"Ожидание DONE для команды 0x{command_code:04x}...")
    start_time = time.time()
    while time.time() - start_time < RESPONSE_TIMEOUT * 2:
        try:
            msg = received_messages_queue.get(timeout=0.1)
            if msg["type"] == "binary" and msg["content"]["command_code"] == command_code and msg["content"]["response_type"] == 0x02:
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

def wait_for_data_and_done(command_code: int, expected_data_len: int, expected_done_status: int = 0x0000) -> tuple[bool, bytes | None]:
    print(f"Ожидание DATA и DONE для команды 0x{command_code:04x}...")
    data_received = None
    done_received = False

    start_time = time.time()
    while time.time() - start_time < RESPONSE_TIMEOUT * 2:
        try:
            msg = received_messages_queue.get(timeout=0.1)
            if msg["type"] == "binary":
                if msg["content"]["command_code"] == command_code:
                    if msg["content"]["response_type"] == 0x03:
                        actual_data_from_packet = msg["content"]["status_or_data"] [3:]
                        if len(actual_data_from_packet) == expected_data_len:
                            data_received = actual_data_from_packet
                            embedded_response_type = msg["content"]["status_or_data"] [0]
                            embedded_status = int.from_bytes(msg["content"]["status_or_data"] [1:3], 'big')
                            print(f"DEBUG: DATA packet embedded Type: 0x{embedded_response_type:02x}, Status: 0x{embedded_status:04x}")
                            print(f"Получен DATA для 0x{command_code:04x}. Данные: {data_received.hex(' ')}")
                        else:
                            print(f"WARNING: Получен DATA с некорректной длиной: {len(actual_data_from_packet)}, ожидалось {expected_data_len}.")
                    elif msg["content"]["response_type"] == 0x02:
                        done_status = int.from_bytes(msg["content"]["status_or_data"], 'big')
                        if done_status == expected_done_status:
                            done_received = True
                            print(f"Получен DONE для 0x{command_code:04x} со статусом 0x{done_status:04x}.")
                        else:
                            print(f"ERROR: Получен DONE со статусом 0x{done_status:04x}, ожидался 0x{expected_done_status:04x}.")
                            return False, None
            else:
                print(f"DEVICE: {msg['content']}")

            if data_received is not None and done_received:
                return True, data_received
        except queue.Empty:
            continue
    print(f"ERROR: Таймаут ожидания DATA и/или DONE для команды 0x{command_code:04x}.")
    return False, None

def wait_for_log_message(partial_log: str, timeout: int = 5) -> bool:
    start_time = time.time()
    while time.time() - start_time < timeout:
        try:
            msg = received_messages_queue.get(timeout=0.1)
            if msg["type"] == "text":
                print(f"DEVICE: {msg['content']}")
                if partial_log in msg["content"]:
                    return True
            elif msg["type"] == "binary":
                print(f"DEVICE (BIN): {msg['content']['raw_packet'].hex(' ')}")
        except queue.Empty:
            continue
    return False

# --- ТЕСТОВЫЕ СЦЕНАРИИ ---
def test_init_command(mask: int):
    print("\n=== Тест команды INIT ===")
    if not send_and_wait_ack(0x1002, mask.to_bytes(1, 'big')):
        return False
    if not wait_for_done(0x1002):
        return False
    print("=== Тест INIT пройден успешно ===")
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
        return True
    print("=== Тест GET_STATUS пройден успешно ===")
    return False

def test_sample_rotate_command(slot_number: int):
    print(f"\n=== Тест команды SAMPLE_ROTATE (0x5110) для слота {slot_number} ===")
    command_code = 0x5110
    params = struct.pack('>H', slot_number)

    if not send_and_wait_ack(command_code, params):
        return False

    expected_log_part = f"Sent ROTATE_MOTOR (ID:4, Steps:"
    if not wait_for_log_message(expected_log_part, timeout=5):
        print(f"ERROR: Log message '{expected_log_part}' not found.")
        return False

    if not wait_for_done(command_code):
        return False

    print(f"=== Тест SAMPLE_ROTATE для слота {slot_number} пройден успешно ===")
    return True

def test_dispenser_aspirate_command(disp_id: int, src_type: int, pos: int, vol: int):
    print(f"\n=== Тест команды DISPENSER_ASPIRATE (0x2100) для дозатора {disp_id}, источник 0x{src_type:02x}, позиция {pos}, объем {vol} мкл ===")
    command_code = 0x2100
    params = struct.pack('>BBHH', disp_id, src_type, pos, vol)

    if not send_and_wait_ack(command_code, params):
        return False

    expected_log_part = f"Sent START_PUMP (ID:{disp_id})"
    if not wait_for_log_message(expected_log_part, timeout=10):
        print(f"ERROR: Log message part '{expected_log_part}' not found for DISPENSER_ASPIRATE.")
        return False

    if not wait_for_done(command_code):
        return False

    print(f"=== Тест DISPENSER_ASPIRATE для дозатора {disp_id} пройден успешно ===")
    return True

# --- ТЕСТ команды DISPENSER_DISPENSE ---
def test_dispenser_dispense_command(disp_id: int, target_type: int, slot: int, vol: int):
    print(f"\n=== Тест команды DISPENSER_DISPENSE (0x2200) для дозатора {disp_id}, назначение 0x{target_type:02x}, слот {slot}, объем {vol} мкл ===")
    command_code = 0x2200
    params = struct.pack('>BBHH', disp_id, target_type, slot, vol)

    if not send_and_wait_ack(command_code, params):
        return False

    expected_log_part = f"Sent START_PUMP (ID:{disp_id})"
    if not wait_for_log_message(expected_log_part, timeout=10):
        print(f"ERROR: Log message part '{expected_log_part}' not found for DISPENSER_DISPENSE.")
        return False

    if not wait_for_done(command_code):
        return False

    print(f"=== Тест DISPENSER_DISPENSE для дозатора {disp_id} пройден успешно ===")
    return True

# --- ТЕСТ команды REAGENT_ROTATE ---
def test_reagent_rotate_command(rotor_id: int, slot_number: int):
    print(f"\n=== Тест команды REAGENT_ROTATE (0x5000) для ротора {rotor_id}, слота {slot_number} ===")
    command_code = 0x5000
    params = struct.pack('>BH', rotor_id, slot_number)

    if not send_and_wait_ack(command_code, params):
        return False

    expected_log_part = f"Sent ROTATE_MOTOR (ID:{6}, Steps:"
    if not wait_for_log_message(expected_log_part, timeout=10):
        print(f"ERROR: Log message part '{expected_log_part}' not found for REAGENT_ROTATE.")
        return False

    if not wait_for_done(command_code):
        return False

    print(f"=== Тест REAGENT_ROTATE для ротора {rotor_id}, слота {slot_number} пройден успешно ===")
    return True

# --- ТЕСТ команды MIXER_MIX ---
def test_mixer_mix_command(mixer_id: int, cuvette: int, duration: int, wash_cycles: int):
    print(f"\n=== Тест команды MIXER_MIX (0x3100) для миксера {mixer_id}, кюветы {cuvette} ===")
    command_code = 0x3100
    # mixer_id (1) + cuvette (2) + duration (2) + wash_cycles (1) = 6 bytes
    params = struct.pack('>BHHB', mixer_id, cuvette, duration, wash_cycles)

    if not send_and_wait_ack(command_code, params):
        return False

    # Проверяем ключевой шаг рецепта по логу для верификации запуска
    expected_log_part = "Sent ROTATE_MOTOR (ID:8, Steps:"
    if not wait_for_log_message(expected_log_part, timeout=10):
        print(f"ERROR: Log message part '{expected_log_part}' not found for MIXER_MIX.")
        return False

    if not wait_for_done(command_code):
        return False

    print(f"=== Тест MIXER_MIX для миксера {mixer_id} пройден успешно ===")
    return True

# --- ТЕСТ команды PHOTOMETER_SCAN_SINGLE ---
def test_photometer_scan_single_command(cuvette: int, wavelength_mask: int) -> bool:
    print(f"\n=== Тест команды PHOTOMETER_SCAN_SINGLE (0x6100) для кюветы {cuvette}, маска 0x{wavelength_mask:02X} ===")
    command_code = 0x6100
    # cuvette (2 bytes) + wavelength_mask (1 byte) = 3 bytes
    params = struct.pack('>HB', cuvette, wavelength_mask)

    if not send_and_wait_ack(command_code, params):
        return False

    # Проверяем, что сканирование было отправлено исполнителю
    expected_log_part = f"Sent PERFORM_SCAN (ID:1, Mask:0x{wavelength_mask:02X})"
    if not wait_for_log_message(expected_log_part, timeout=10):
        print(f"ERROR: Log message part '{expected_log_part}' not found for PHOTOMETER_SCAN_SINGLE.")
        return False

    if not wait_for_done(command_code):
        return False

    print(f"=== Тест PHOTOMETER_SCAN_SINGLE для кюветы {cuvette} пройден успешно ===")
    return True

# --- ТЕСТ команды WASH_STATION_FILL ---
def test_wash_station_fill_command(cuvette: int, volume: int) -> bool:
    print(f"\n=== Тест команды WASH_STATION_FILL (0x4100) для кюветы {cuvette}, объем {volume} мкл ===")
    command_code = 0x4100
    # cuvette (2 байта, big-endian) + volume (2 байта, big-endian)
    params = struct.pack('>HH', cuvette, volume)

    if not send_and_wait_ack(command_code, params):
        return False

    # Проверяем, что лог о запуске насоса был отправлен
    # Assuming Pump ID 2 for wash station fill, from recipe_store.c
    expected_log_part_pump = f"Sent START_PUMP (ID:2)"
    if not wait_for_log_message(expected_log_part_pump, timeout=5):
        print(f"ERROR: Log message '{expected_log_part_pump}' not found in DEVICE logs for WASH_STATION_FILL (pump start).")
        return False

    if not wait_for_done(command_code):
        return False

    print(f"=== Тест WASH_STATION_FILL для кюветы {cuvette}, объем {volume} мкл пройден успешно ===")
    return True


# --- ТЕСТ команды WASH_STATION_WASH ---
def test_wash_station_wash_command(cycles: int, cuvette: int) -> bool:
    print(f"\n=== Тест команды WASH_STATION_WASH (0x4000) для {cycles} циклов, кюветы {cuvette} ===")

    # cycles (1 байт) + cuvette (2 байта, big-endian)
    params = struct.pack('>BH', cycles, cuvette)

    if not send_and_wait_ack(0x4000, params):
        return False

    # JobManager должен будет отправить логи о ROTATE_MOTOR с правильным количеством шагов
    expected_log_part = f"Sent ROTATE_MOTOR (ID:3, Steps:" # ID 3 for reaction disk
    if not wait_for_log_message(expected_log_part, timeout=5): # timeout for log message
        print(f"ERROR: Log message '{expected_log_part}' not found in DEVICE logs for WASH_STATION_WASH.")
        return False

    if not wait_for_done(0x4000):
        return False

    print(f"=== Тест WASH_STATION_WASH для {cycles} циклов, кюветы {cuvette} пройден успешно ===")
    return True


# --- ТЕСТ команды DISPENSER_WASH ---
def test_dispenser_wash_command(dispenser_id: int, volume: int, cycles: int) -> bool:
    print(f"\n=== Тест команды DISPENSER_WASH (0x2000) для дозатора {dispenser_id}, объем {volume} мкл, циклов {cycles} ===")
    command_code = 0x2000
    # dispenser_id (1) + volume (2) + cycles (1) = 4 bytes
    params = struct.pack('>BHB', dispenser_id, volume, cycles)

    if not send_and_wait_ack(command_code, params):
        return False

    # Проверяем, что первая атомарная операция ROTATE_MOTOR (для вращения X-Y) была отправлена
    expected_log_rot = "Sent ROTATE_MOTOR (ID:5, Steps:"
    if not wait_for_log_message(expected_log_rot, timeout=10):
        print(f"ERROR: Log message part '{expected_log_rot}' not found for DISPENSER_WASH (rotation).")
        return False

    if not wait_for_done(command_code):
        return False

    print(f"=== Тест DISPENSER_WASH для дозатора {dispenser_id} пройден успешно ===")
    return True


# --- ГЛАВНАЯ ФУНКЦИЯ ---
def main():
    global ser
    mask = 0xFF 

    print(f"Попытка подключения к порту {SERIAL_PORT}...")
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0)
        print("Соединение установлено.")
        time.sleep(2)

        ser.reset_input_buffer()
        ser.reset_output_buffer()
        
        listener_thread = threading.Thread(target=listen_serial_port)
        listener_thread.start()
        print("Запущен поток прослушивания.")

        while not received_messages_queue.empty():
            received_messages_queue.get_nowait()

        # --- ЗАПУСК ТЕСТОВОГО СЦЕНАРИЯ "ЦИКЛ АНАЛИЗА ОБРАЗЦА" ---
        print("\n" + "="*20 + " НАЧАЛО ТЕСТА: ЦИКЛ АНАЛИЗА ОБРАЗЦА " + "="*20)
        all_tests_passed = True
        
        # 1. Инициализация системы
        if not test_init_command(mask):
             all_tests_passed = False

        # 2. Проверка статуса
        if all_tests_passed and not test_get_status_command():
            all_tests_passed = False

        # 3. Поворот диска с образцами к позиции забора (например, слот 5)
        if all_tests_passed and not test_sample_rotate_command(5):
            all_tests_passed = False

        # 4. Забор образца дозатором
        # (дозатор 1, источник - диск образцов (0x03), позиция 5, объем 10 мкл)
        if all_tests_passed and not test_dispenser_aspirate_command(1, 0x03, 5, 10):
            all_tests_passed = False
        
        # 5. Выдача образца в кювету
        # (дозатор 1, назначение - реакционный диск (0x01), кювета 10, объем 10 мкл)
        if all_tests_passed and not test_dispenser_dispense_command(1, 0x01, 10, 10):
            all_tests_passed = False

        # 6. Поворот ротора реагентов
        # (ротор 1, слот 3)
        if all_tests_passed and not test_reagent_rotate_command(1, 3):
            all_tests_passed = False

        # 7. Забор реагента дозатором (после поворота ротора)
        # (дозатор 1, источник - ротор реагентов (0x02), позиция 3, объем 200 мкл)
        if all_tests_passed and not test_dispenser_aspirate_command(1, 0x02, 3, 200):
            all_tests_passed = False

        # 8. Выдача реагента в кювету
        # (дозатор 1, назначение - реакционный диск (0x01), кювета 10, объем 200 мкл)
        if all_tests_passed and not test_dispenser_dispense_command(1, 0x01, 10, 200):
            all_tests_passed = False
            
        # 9. Перемешивание содержимого кюветы
        # (миксер 1, кювета 10, время 3000 мс, 2 цикла промывки)
        if all_tests_passed and not test_mixer_mix_command(1, 10, 3000, 2):
            all_tests_passed = False

        # 10. Фотометрическое сканирование кюветы
        # (кювета 10, маска длин волн 0x03)
        if all_tests_passed and not test_photometer_scan_single_command(10, 0x03):
            all_tests_passed = False

        # --- ЗАПУСК ТЕСТОВОГО СЦЕНАРИЯ "ПРОМЫВКА СИСТЕМЫ" ---
        print("\n" + "="*20 + " НАЧАЛО ТЕСТА: ПРОМЫВКА СИСТЕМЫ " + "="*20)

        # 11. Полная промывка дозатора
        # (дозатор 1, объем 1000 мкл, 5 циклов)
        if all_tests_passed and not test_dispenser_wash_command(1, 1000, 5):
            all_tests_passed = False

        # 12. Заполнение моечной станции
        # (кювета 10, объем 500 мкл)
        if all_tests_passed and not test_wash_station_fill_command(10, 500):
            all_tests_passed = False

        # 13. Промывка моечной станции
        # (3 цикла, кювета 10 - параметры из test_combined_commands)
        if all_tests_passed and not test_wash_station_wash_command(3, 10):
            all_tests_passed = False
            
        print("\n" + "="*20 + " ЗАВЕРШЕНИЕ ТЕСТА: ПРОМЫВКА СИСТЕМЫ " + "="*20)


        print("\n" + "="*20 + " ЗАВЕРШЕНИЕ ТЕСТА: ЦИКЛ АНАЛИЗА ОБРАЗЦА " + "="*20)
        if all_tests_passed:
            print("\nНа данный момент все реализованные шаги цикла анализа пройдены успешно!")
        else:
            print("\nОШИБКА: Один из шагов цикла анализа образца провален!")

    except serial.SerialException as e:
        print(f"Ошибка: Не удалось открыть порт {SERIAL_PORT}. {e}")
    except Exception as e:
        print(f"Произошла непредвиденная ошибка: {e}")
    finally:
        stop_listening_event.set()
        if 'listener_thread' in locals() and listener_thread.is_alive():
            listener_thread.join(timeout=2)
        
        if ser and ser.is_open:
            ser.close()
            print("Соединение закрыто.")

if __name__ == "__main__":
    main()