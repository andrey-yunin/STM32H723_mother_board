#!/usr/bin/env python3
"""
Host-level acceptance scenarios for the STM32H723 Conductor.

The script intentionally validates only the public Host protocol:
ACK/NACK/ERROR, DATA and DONE. It does not use text logs as pass criteria,
because firmware logs are now routed through a separate logger boundary.
"""

from __future__ import annotations

import argparse
import queue
import struct
import sys
import threading
import time
from collections.abc import Callable

try:
    import serial
except ImportError:
    serial = None

SerialException = serial.SerialException if serial is not None else OSError

# Defaults are kept as module globals because can_test.py imports this module
# as the Host protocol helper.
SERIAL_PORT = "/dev/ttyACM0"
BAUD_RATE = 9600
RESPONSE_TIMEOUT = 5.0

received_messages_queue: queue.Queue[dict] = queue.Queue()
stop_listening_event = threading.Event()
ser: serial.Serial | None = None


HOST_RESPONSE_TYPE_NACK = 0x00
HOST_RESPONSE_TYPE_ACK = 0x01
HOST_RESPONSE_TYPE_DONE = 0x02
HOST_RESPONSE_TYPE_DATA = 0x03
HOST_RESPONSE_TYPE_ERROR = 0x04

HOST_STATUS_OK = 0x0000
HOST_ERR_UNKNOWN_CMD = 0x0002
HOST_ERR_INVALID_PARAM = 0x0003
HOST_ERR_CRC = 0x0007
HOST_ERR_NOT_SUPPORTED = 0x000A

CMD_GET_STATUS = 0x1000
CMD_INIT = 0x1002
CMD_GET_VERSION = 0x1003
CMD_GET_DATETIME = 0x1005
CMD_EMERGENCY_STOP = 0x1010
CMD_DISPENSER_WASH = 0x2000
CMD_DISPENSER_ASPIRATE = 0x2100
CMD_DISPENSER_DISPENSE = 0x2200
CMD_MIXER_MIX = 0x3100
CMD_WASH_STATION_WASH = 0x4000
CMD_WASH_STATION_FILL = 0x4100
CMD_REAGENT_ROTATE = 0x5000
CMD_SAMPLE_ROTATE = 0x5110
CMD_PHOTOMETER_SCAN_SINGLE = 0x6100
CMD_THERMO_GET_TEMP_LEGACY = 0x8000
CMD_SENSOR_GET_ALL_TEMPS = 0x9010
CMD_SENSOR_GET_TEMP = 0x9011


TYPE_NAMES = {
    HOST_RESPONSE_TYPE_NACK: "NACK",
    HOST_RESPONSE_TYPE_ACK: "ACK",
    HOST_RESPONSE_TYPE_DONE: "DONE",
    HOST_RESPONSE_TYPE_DATA: "DATA",
    HOST_RESPONSE_TYPE_ERROR: "ERROR",
}


def calculate_crc(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
    return crc


def build_command(command_code: int, params: bytes = b"") -> bytes:
    header = b"CM>"
    command_bytes = command_code.to_bytes(2, "big")
    crc_payload = command_bytes + params
    payload_len = len(crc_payload) + 1
    crc = calculate_crc(crc_payload).to_bytes(1, "big")
    return header + payload_len.to_bytes(2, "big") + crc_payload + crc


def parse_response_packet(raw_data: bytes):
    if len(raw_data) < 8 or not raw_data.startswith(b"CM>"):
        return None, raw_data

    try:
        payload_len = int.from_bytes(raw_data[3:5], "big")
        if len(raw_data) < 5 + payload_len:
            return None, raw_data

        packet_payload = raw_data[5 : 5 + payload_len]
        command_code = int.from_bytes(packet_payload[0:2], "big")

        if payload_len == 6:
            response_type = packet_payload[2]
            status_or_data = packet_payload[3:-1]
        elif payload_len > 6:
            response_type = HOST_RESPONSE_TYPE_DATA
            status_or_data = packet_payload[2:-1]
        else:
            response_type = 0xFF
            status_or_data = packet_payload[2:-1]

        received_crc = packet_payload[-1]
        calculated_crc = calculate_crc(packet_payload[:-1])
        if calculated_crc != received_crc:
            print(
                f"ERROR: CRC mismatch for 0x{command_code:04X}. "
                f"Raw: {raw_data.hex(' ')}"
            )
            return None, raw_data

        return {
            "command_code": command_code,
            "response_type": response_type,
            "status_or_data": status_or_data,
            "raw_packet": raw_data[: 5 + payload_len],
        }, raw_data[5 + payload_len :]
    except Exception as exc:
        print(f"ERROR: response parse failed: {exc}. Raw: {raw_data.hex(' ')}")
        return None, raw_data


def listen_serial_port() -> None:
    global ser
    if ser is None or not ser.is_open:
        print("ERROR: serial port is not open.")
        return

    buffer = b""
    while not stop_listening_event.is_set():
        try:
            byte = ser.read(1)
            if not byte:
                time.sleep(0.01)
                continue

            buffer += byte
            while True:
                cm_index = buffer.find(b"CM>")
                if cm_index >= 0:
                    if cm_index > 0:
                        text = buffer[:cm_index].decode("utf-8", errors="ignore").strip()
                        if text:
                            received_messages_queue.put({"type": "text", "content": text})
                        buffer = buffer[cm_index:]

                    response, remaining = parse_response_packet(buffer)
                    if response is None:
                        break
                    received_messages_queue.put({"type": "binary", "content": response})
                    buffer = remaining
                    continue

                newline_index = buffer.find(b"\n")
                if newline_index < 0:
                    break

                text = buffer[:newline_index].decode("utf-8", errors="ignore").strip()
                if text:
                    received_messages_queue.put({"type": "text", "content": text})
                buffer = buffer[newline_index + 1 :]
        except SerialException as exc:
            print(f"ERROR: serial listener failed: {exc}")
            break
        except Exception as exc:
            print(f"ERROR: unexpected listener failure: {exc}")
            break

    print("Listener thread stopped.")


def clear_queue() -> None:
    while True:
        try:
            received_messages_queue.get_nowait()
        except queue.Empty:
            return


def format_binary_message(msg: dict) -> str:
    content = msg["content"]
    command_code = content["command_code"]
    response_type = content["response_type"]
    type_name = TYPE_NAMES.get(response_type, f"TYPE_0x{response_type:02X}")
    data = content["status_or_data"]

    if response_type in (
        HOST_RESPONSE_TYPE_NACK,
        HOST_RESPONSE_TYPE_ACK,
        HOST_RESPONSE_TYPE_DONE,
        HOST_RESPONSE_TYPE_ERROR,
    ) and len(data) >= 2:
        status = int.from_bytes(data[:2], "big")
        return f"{type_name} cmd=0x{command_code:04X} status=0x{status:04X}"

    if response_type == HOST_RESPONSE_TYPE_DATA and len(data) >= 3:
        embedded_type = data[0]
        embedded_status = int.from_bytes(data[1:3], "big")
        payload = data[3:]
        return (
            f"DATA cmd=0x{command_code:04X} embedded=0x{embedded_type:02X} "
            f"status=0x{embedded_status:04X} payload={payload.hex(' ')}"
        )

    return f"{type_name} cmd=0x{command_code:04X} data={data.hex(' ')}"


def print_message(msg: dict) -> None:
    if msg["type"] == "text":
        print(f"DEVICE TEXT: {msg['content']}")
    elif msg["type"] == "binary":
        print(f"DEVICE BIN: {format_binary_message(msg)}")


def send_command(command_code: int, params: bytes = b"") -> None:
    if ser is None:
        raise RuntimeError("serial port is not open")

    packet = build_command(command_code, params)
    print(f"HOST -> 0x{command_code:04X}: {packet.hex(' ')}")
    ser.write(packet)
    ser.flush()


def send_raw_packet(packet: bytes, label: str) -> None:
    if ser is None:
        raise RuntimeError("serial port is not open")

    print(f"HOST -> {label}: {packet.hex(' ')}")
    ser.write(packet)
    ser.flush()


def wait_for_response(
    command_code: int,
    response_types: set[int],
    timeout_s: float | None = None,
) -> dict | None:
    timeout = RESPONSE_TIMEOUT if timeout_s is None else timeout_s
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        try:
            msg = received_messages_queue.get(timeout=0.1)
        except queue.Empty:
            continue

        print_message(msg)
        if msg["type"] != "binary":
            continue

        content = msg["content"]
        if (
            content["command_code"] == command_code
            and content["response_type"] in response_types
        ):
            return msg

    expected = ", ".join(TYPE_NAMES.get(x, f"0x{x:02X}") for x in sorted(response_types))
    print(f"ERROR: timeout waiting for 0x{command_code:04X} response in {{{expected}}}.")
    return None


def response_status(msg: dict) -> int:
    data = msg["content"]["status_or_data"]
    return int.from_bytes(data[:2], "big") if len(data) >= 2 else 0xFFFF


def send_and_expect_ack(command_code: int, params: bytes = b"") -> bool:
    send_command(command_code, params)
    msg = wait_for_response(
        command_code,
        {HOST_RESPONSE_TYPE_ACK, HOST_RESPONSE_TYPE_NACK, HOST_RESPONSE_TYPE_ERROR},
    )
    if msg is None:
        return False

    response_type = msg["content"]["response_type"]
    if response_type != HOST_RESPONSE_TYPE_ACK:
        print(
            f"ERROR: 0x{command_code:04X} expected ACK, got "
            f"{TYPE_NAMES.get(response_type, response_type)} status=0x{response_status(msg):04X}."
        )
        return False
    return True


def wait_for_done(command_code: int, expected_status: int = HOST_STATUS_OK) -> bool:
    msg = wait_for_response(command_code, {HOST_RESPONSE_TYPE_DONE})
    if msg is None:
        return False

    status = response_status(msg)
    if status != expected_status:
        print(
            f"ERROR: DONE 0x{command_code:04X} status=0x{status:04X}, "
            f"expected 0x{expected_status:04X}."
        )
        return False
    return True


def wait_for_error(command_code: int, expected_status: int) -> bool:
    msg = wait_for_response(command_code, {HOST_RESPONSE_TYPE_ERROR})
    if msg is None:
        return False

    status = response_status(msg)
    if status != expected_status:
        print(
            f"ERROR: ERROR 0x{command_code:04X} status=0x{status:04X}, "
            f"expected 0x{expected_status:04X}."
        )
        return False
    return True


def wait_for_data_and_done(
    command_code: int,
    expected_data_len: int | None = None,
    expected_done_status: int = HOST_STATUS_OK,
) -> tuple[bool, bytes | None]:
    data_payload: bytes | None = None
    done_received = False
    deadline = time.monotonic() + RESPONSE_TIMEOUT * 2

    while time.monotonic() < deadline:
        try:
            msg = received_messages_queue.get(timeout=0.1)
        except queue.Empty:
            continue

        print_message(msg)
        if msg["type"] != "binary":
            continue

        content = msg["content"]
        if content["command_code"] != command_code:
            continue

        if content["response_type"] == HOST_RESPONSE_TYPE_DATA:
            raw = content["status_or_data"]
            if len(raw) < 3:
                print(f"ERROR: malformed DATA for 0x{command_code:04X}.")
                return False, None

            embedded_type = raw[0]
            embedded_status = int.from_bytes(raw[1:3], "big")
            payload = raw[3:]

            if embedded_type != HOST_RESPONSE_TYPE_DATA or embedded_status != HOST_STATUS_OK:
                print(
                    f"ERROR: DATA 0x{command_code:04X} embedded=0x{embedded_type:02X} "
                    f"status=0x{embedded_status:04X}."
                )
                return False, None

            if expected_data_len is not None and len(payload) != expected_data_len:
                print(
                    f"ERROR: DATA 0x{command_code:04X} length={len(payload)}, "
                    f"expected {expected_data_len}."
                )
                return False, None

            data_payload = payload

        elif content["response_type"] == HOST_RESPONSE_TYPE_DONE:
            status = response_status(msg)
            if status != expected_done_status:
                print(
                    f"ERROR: DONE 0x{command_code:04X} status=0x{status:04X}, "
                    f"expected 0x{expected_done_status:04X}."
                )
                return False, None
            done_received = True

        if data_payload is not None and done_received:
            return True, data_payload

    print(f"ERROR: timeout waiting DATA + DONE for 0x{command_code:04X}.")
    return False, None


def run_ack_done_test(name: str, command_code: int, params: bytes = b"") -> bool:
    print(f"\n=== {name} 0x{command_code:04X} ===")
    return send_and_expect_ack(command_code, params) and wait_for_done(command_code)


def test_init_command(mask: int) -> bool:
    return run_ack_done_test("INIT", CMD_INIT, bytes([mask & 0xFF]))


def test_get_status_command() -> bool:
    print("\n=== GET_STATUS 0x1000 ===")
    if not send_and_expect_ack(CMD_GET_STATUS):
        return False

    success, data = wait_for_data_and_done(CMD_GET_STATUS, expected_data_len=3)
    if not success or data is None:
        return False

    state = data[0]
    last_error = int.from_bytes(data[1:3], "big")
    print(f"GET_STATUS parsed: state=0x{state:02X}, last_error=0x{last_error:04X}")
    return True


def test_get_version_command() -> bool:
    print("\n=== GET_VERSION 0x1003 ===")
    if not send_and_expect_ack(CMD_GET_VERSION):
        return False
    success, data = wait_for_data_and_done(CMD_GET_VERSION)
    if success and data is not None:
        print(f"GET_VERSION payload: {data.hex(' ')}")
    return success


def test_get_datetime_command() -> bool:
    print("\n=== GET_DATETIME 0x1005 ===")
    if not send_and_expect_ack(CMD_GET_DATETIME):
        return False
    success, data = wait_for_data_and_done(CMD_GET_DATETIME)
    if success and data is not None:
        print(f"GET_DATETIME payload: {data.hex(' ')}")
    return success


def test_get_status_bad_crc() -> bool:
    print("\n=== Protocol negative: GET_STATUS bad CRC ===")
    packet = bytearray(build_command(CMD_GET_STATUS))
    packet[-1] ^= 0xFF
    send_raw_packet(bytes(packet), "0x1000 bad CRC")

    msg = wait_for_response(CMD_GET_STATUS, {HOST_RESPONSE_TYPE_NACK})
    if msg is None:
        return False

    status = response_status(msg)
    if status != HOST_ERR_CRC:
        print(f"ERROR: bad CRC expected NACK 0x{HOST_ERR_CRC:04X}, got 0x{status:04X}.")
        return False
    return True


def test_get_status_invalid_params() -> bool:
    print("\n=== Protocol negative: GET_STATUS invalid params ===")
    send_command(CMD_GET_STATUS, b"\x00")

    msg = wait_for_response(CMD_GET_STATUS, {HOST_RESPONSE_TYPE_NACK})
    if msg is None:
        return False

    status = response_status(msg)
    if status != HOST_ERR_INVALID_PARAM:
        print(
            f"ERROR: invalid params expected NACK 0x{HOST_ERR_INVALID_PARAM:04X}, "
            f"got 0x{status:04X}."
        )
        return False
    return True


def test_unknown_command() -> bool:
    print("\n=== UNKNOWN COMMAND 0xFFFF ===")
    send_command(0xFFFF)
    return wait_for_error(0xFFFF, HOST_ERR_UNKNOWN_CMD)


def test_legacy_thermo_get_temp_unsupported() -> bool:
    print("\n=== Legacy THERMO_GET_TEMP 0x8000 must stay unsupported ===")
    send_command(CMD_THERMO_GET_TEMP_LEGACY, b"\x01")
    return wait_for_error(CMD_THERMO_GET_TEMP_LEGACY, HOST_ERR_NOT_SUPPORTED)


def test_photometer_scan_single_unsupported(cuvette: int, wavelength_mask: int) -> bool:
    print("\n=== PHOTOMETER_SCAN_SINGLE explicit unsupported baseline ===")
    params = struct.pack(">HB", cuvette, wavelength_mask)
    if not send_and_expect_ack(CMD_PHOTOMETER_SCAN_SINGLE, params):
        return False
    return wait_for_done(CMD_PHOTOMETER_SCAN_SINGLE, HOST_ERR_NOT_SUPPORTED)


def test_sensor_get_temp(sensor_id: int) -> bool:
    print(f"\n=== SENSOR_GET_TEMP 0x9011 sensor_id={sensor_id} ===")
    if not send_and_expect_ack(CMD_SENSOR_GET_TEMP, bytes([sensor_id & 0xFF])):
        return False

    success, data = wait_for_data_and_done(CMD_SENSOR_GET_TEMP, expected_data_len=4)
    if not success or data is None:
        return False

    returned_sensor_id = data[0]
    temperature = int.from_bytes(data[1:3], "big", signed=True)
    status = data[3]
    print(
        f"SENSOR_GET_TEMP parsed: sensor_id={returned_sensor_id}, "
        f"temperature={temperature / 10:.1f} C, status={status}"
    )
    return returned_sensor_id == sensor_id


def test_sensor_get_all_temps() -> bool:
    print("\n=== SENSOR_GET_ALL_TEMPS 0x9010 ===")
    if not send_and_expect_ack(CMD_SENSOR_GET_ALL_TEMPS):
        return False

    success, data = wait_for_data_and_done(CMD_SENSOR_GET_ALL_TEMPS)
    if not success or data is None:
        return False

    if len(data) < 1:
        print("ERROR: GET_ALL_TEMPS returned empty payload.")
        return False

    count = data[0]
    expected_len = 1 + count * 4
    if len(data) != expected_len:
        print(f"ERROR: GET_ALL_TEMPS length={len(data)}, expected {expected_len}.")
        return False

    for idx in range(count):
        offset = 1 + idx * 4
        sensor_id = data[offset]
        temperature = int.from_bytes(data[offset + 1 : offset + 3], "big", signed=True)
        status = data[offset + 3]
        print(f"  sensor_id={sensor_id}, temperature={temperature / 10:.1f} C, status={status}")
    return True


def test_emergency_stop() -> bool:
    print("\n=== EMERGENCY_STOP 0x1010 ===")
    return send_and_expect_ack(CMD_EMERGENCY_STOP) and wait_for_done(CMD_EMERGENCY_STOP)


def build_recipe_tests() -> list[tuple[str, int, bytes]]:
    return [
        ("DISPENSER_WASH", CMD_DISPENSER_WASH, struct.pack(">BHB", 1, 1000, 5)),
        ("WASH_STATION_WASH", CMD_WASH_STATION_WASH, struct.pack(">BH", 3, 10)),
        ("WASH_STATION_FILL", CMD_WASH_STATION_FILL, struct.pack(">HH", 500, 10)),
        ("SAMPLE_ROTATE", CMD_SAMPLE_ROTATE, struct.pack(">H", 5)),
        ("DISPENSER_ASPIRATE sample", CMD_DISPENSER_ASPIRATE, struct.pack(">BBHH", 1, 0x03, 5, 10)),
        ("DISPENSER_DISPENSE sample", CMD_DISPENSER_DISPENSE, struct.pack(">BBHH", 1, 0x01, 10, 10)),
        ("REAGENT_ROTATE", CMD_REAGENT_ROTATE, struct.pack(">BH", 1, 3)),
        ("DISPENSER_ASPIRATE reagent", CMD_DISPENSER_ASPIRATE, struct.pack(">BBHH", 1, 0x02, 3, 200)),
        ("DISPENSER_DISPENSE reagent", CMD_DISPENSER_DISPENSE, struct.pack(">BBHH", 1, 0x01, 10, 200)),
        ("MIXER_MIX", CMD_MIXER_MIX, struct.pack(">BHHB", 1, 10, 3000, 2)),
    ]


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="DDS-240 Conductor Host protocol acceptance script",
    )
    parser.add_argument("-s", "--serial", default=SERIAL_PORT)
    parser.add_argument("--timeout", type=float, default=RESPONSE_TIMEOUT)
    parser.add_argument("--mask", type=lambda value: int(value, 0), default=0x01)
    parser.add_argument("--skip-init", action="store_true")
    parser.add_argument("--skip-recipes", action="store_true")
    parser.add_argument(
        "--host-only",
        action="store_true",
        help="Run only USB Host/parser/local-direct checks that do not require CAN executors.",
    )
    parser.add_argument(
        "--listen-only",
        action="store_true",
        help="Open the USB CDC port and print incoming text/binary messages without sending commands.",
    )
    parser.add_argument(
        "--listen-duration",
        type=float,
        default=10.0,
        help="Duration for --listen-only, seconds.",
    )
    parser.add_argument("--include-thermo", action="store_true")
    parser.add_argument("--include-unsupported", action="store_true")
    parser.add_argument(
        "--include-emergency",
        action="store_true",
        help="Runs EMERGENCY_STOP; system will require INIT/recovery afterwards.",
    )
    return parser.parse_args(argv)


def open_serial(serial_port: str) -> threading.Thread:
    global ser
    if serial is None:
        raise RuntimeError(
            "pyserial is not installed. Activate App_user/.venv or run: "
            "python3 -m pip install pyserial"
        )

    print(f"Opening serial port {serial_port}...")
    ser = serial.Serial(serial_port, BAUD_RATE, timeout=0)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    clear_queue()

    stop_listening_event.clear()
    listener_thread = threading.Thread(target=listen_serial_port, daemon=True)
    listener_thread.start()
    return listener_thread


def close_serial(listener_thread: threading.Thread | None) -> None:
    global ser
    stop_listening_event.set()
    if listener_thread is not None:
        listener_thread.join(timeout=2)
    if ser is not None and ser.is_open:
        ser.close()
        print("Serial port closed.")


def main(argv: list[str] | None = None) -> int:
    global RESPONSE_TIMEOUT
    args = parse_args(argv)
    RESPONSE_TIMEOUT = args.timeout

    listener_thread: threading.Thread | None = None
    host_only_tests: list[tuple[str, Callable[[], bool]]] = [
        ("GET_STATUS", test_get_status_command),
        ("GET_VERSION", test_get_version_command),
        ("GET_DATETIME", test_get_datetime_command),
        ("GET_STATUS bad CRC", test_get_status_bad_crc),
        ("GET_STATUS invalid params", test_get_status_invalid_params),
    ]

    tests: list[tuple[str, Callable[[], bool]]] = []

    if args.host_only:
        tests.extend(host_only_tests)
    else:
        if not args.skip_init:
            tests.append(("INIT", lambda: test_init_command(args.mask)))

        tests.extend(host_only_tests[:3])

        if not args.skip_recipes:
            for name, command_code, params in build_recipe_tests():
                tests.append((name, lambda c=command_code, p=params, n=name: run_ack_done_test(n, c, p)))

        if args.include_unsupported:
            tests.append(("PHOTOMETER unsupported", lambda: test_photometer_scan_single_unsupported(10, 0x03)))
            tests.append(("Legacy 0x8000 unsupported", test_legacy_thermo_get_temp_unsupported))
            tests.append(("Unknown command", test_unknown_command))

        if args.include_thermo:
            tests.append(("SENSOR_GET_TEMP", lambda: test_sensor_get_temp(1)))
            tests.append(("SENSOR_GET_ALL_TEMPS", test_sensor_get_all_temps))

        if args.include_emergency:
            tests.append(("EMERGENCY_STOP", test_emergency_stop))

    try:
        listener_thread = open_serial(args.serial)

        if args.listen_only:
            print(f"Listening for {args.listen_duration:.1f}s without sending Host commands...")
            deadline = time.monotonic() + args.listen_duration
            received_any = False
            while time.monotonic() < deadline:
                try:
                    msg = received_messages_queue.get(timeout=0.2)
                except queue.Empty:
                    continue
                received_any = True
                print_message(msg)

            if not received_any:
                print("No bytes/messages received from the device during listen window.")
            return 0 if received_any else 1

        all_ok = True
        for name, test in tests:
            if not test():
                print(f"\nFAILED: {name}")
                all_ok = False
                break

        print("\nPASS: all selected tests completed." if all_ok else "\nFAIL: selected test set failed.")
        return 0 if all_ok else 1
    except (RuntimeError, SerialException) as exc:
        print(f"ERROR: cannot open serial port {args.serial}: {exc}")
        return 2
    except KeyboardInterrupt:
        print("\nInterrupted.")
        return 130
    finally:
        close_serial(listener_thread)


if __name__ == "__main__":
    sys.exit(main())
