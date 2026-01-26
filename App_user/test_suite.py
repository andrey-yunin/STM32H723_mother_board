import serial
import time
import sys

# --- НАСТРОЙКИ ---
SERIAL_PORT = '/dev/ttyACM5'
BAUD_RATE = 9600
RESPONSE_TIMEOUT = 5  # Таймаут для ожидания ответов от устройства

# --- Вспомогательные функции ---

def calculate_crc(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
    return crc

def build_command(command_code: int, params: bytes = b'') -> bytes:
    header = b'CM>'
    command_bytes = command_code.to_bytes(2, 'big')
    length = len(command_bytes) + len(params) + 1  # Cmd + Params + CRC
    length_bytes = length.to_bytes(2, 'big')
    crc_payload = command_bytes + params
    crc = calculate_crc(crc_payload).to_bytes(1, 'big')
    packet = header + length_bytes + crc_payload + crc
    return packet

def read_full_response(ser: serial.Serial, command_code: int, timeout: int = RESPONSE_TIMEOUT) -> list:
    responses = []
    start_time = time.time()
    
    # Буфер для накопления всех байтов, прочитанных с порта
    serial_buffer = b'' 
    
    while time.time() - start_time < timeout:
        # Читаем все доступные байты из порта
        bytes_in_waiting = ser.in_waiting
        if bytes_in_waiting > 0:
            serial_buffer += ser.read(bytes_in_waiting)
        else:
            time.sleep(0.01) # Небольшая задержка, чтобы не загружать CPU

        # Поиск заголовка бинарного пакета 'CM>'
        while b'CM>' in serial_buffer:
            header_start_index = serial_buffer.find(b'CM>')
            
            # Все, что до заголовка - это отладочное сообщение
            if header_start_index > 0:
                debug_chunk = serial_buffer[:header_start_index]
                try:
                    debug_msg = debug_chunk.decode('utf-8', errors='ignore').strip()
                    if debug_msg:
                        print(f"DEVICE DEBUG: {debug_msg}")
                except UnicodeDecodeError:
                    print(f"DEVICE RAW UNEXPECTED: {debug_chunk.hex(' ')}")
                
                # Удаляем отладочное сообщение из буфера
                serial_buffer = serial_buffer[header_start_index:]
                header_start_index = 0 # Заголовок теперь в начале буфера

            # Теперь serial_buffer должен начинаться с 'CM>'
            if len(serial_buffer) >= 5: # Заголовок (3) + Длина Payload (2)
                response_header = serial_buffer[0:3]
                response_length_bytes = serial_buffer[3:5]
                response_length = int.from_bytes(response_length_bytes, 'big')

                # Проверяем, есть ли полный пакет в буфере
                full_packet_length = 3 + 2 + response_length # Header(3) + Len(2) + Payload(response_length)
                
                if len(serial_buffer) >= full_packet_length:
                    full_response_data = serial_buffer[:full_packet_length]
                    
                    # Удаляем обработанный пакет из буфера
                    serial_buffer = serial_buffer[full_packet_length:]

                    # Парсинг ответа
                    # response_body = CmdCode(2) + Type(1) + Status/Data(X) + CRC(1)
                    # response_body начинается после 5 байт (3 заголовка + 2 длины)
                    body_offset = 5
                    current_response_body = full_response_data[body_offset:]

                    if len(current_response_body) < 4: # Минимальный размер: 2 CmdCode + 1 Type + 1 CRC
                        print(f"WARNING: Response body too short for parsing. RAW: {current_response_body.hex(' ')}")
                        continue # Пробуем следующий пакет, если есть
                    
                    cmd_code_resp = int.from_bytes(current_response_body[0:2], 'big')
                    response_type = current_response_body[2]
                    
                    status_or_data_raw = current_response_body[3:-1] 
                    
                    response_status = 0 # По умолчанию OK
                    response_data = b''

                    if response_type == 0x01 or response_type == 0x02 or response_type == 0x04: # ACK, DONE, NACK/ERROR (статус 2 байта)
                        if len(status_or_data_raw) >= 2:
                            response_status = int.from_bytes(status_or_data_raw[0:2], 'big')
                        else:
                            print(f"WARNING: Incomplete status bytes for type 0x{response_type:02x} for cmd 0x{command_code:04x}.")
                    elif response_type == 0x03: # DATA (данные переменной длины)
                        response_data = status_or_data_raw
                    
                    calculated_crc = calculate_crc(current_response_body[0:-1])
                    received_crc = current_response_body[-1]
                    crc_ok = (calculated_crc == received_crc)
                    
                    response_info = {
                        "command_code": cmd_code_resp,
                        "response_type": response_type,
                        "status": response_status,
                        "data": response_data,
                        "crc_ok": crc_ok,
                        "full_raw": full_response_data
                    }
                    responses.append(response_info)

                    # Если это DONE/NACK/ERROR, это обычно конец последовательности, можно выйти раньше
                    if response_type in [0x02, 0x04]:
                        return responses # Возвращаем ответы сразу

                else: # Пакет не полностью пришел, ждем еще байтов
                    break # Выходим из внутреннего цикла, чтобы прочитать еще байты из порта
            else: # Недостаточно байтов для чтения длины, ждем еще
                break # Выходим из внутреннего цикла, чтобы прочитать еще байты из порта
        
    return responses