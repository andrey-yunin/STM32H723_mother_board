#!/usr/bin/env python3
"""
Smoke test for the current Conductor + CANable bench.

What it checks:
1. USB command path works by sending INIT (0x1002).
2. Conductor receives fake executor frames from CANable.
3. Conductor can complete INIT after fake HOME_MOTOR DONE frames.

The script intentionally does not flash firmware and does not touch project files.
"""

from __future__ import annotations

import argparse
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

usb_proto = None

CMD_INIT = 0x1002
CMD_GET_STATUS = 0x1000

CAN_PRIORITY_NORMAL = 1
CAN_MSG_TYPE_COMMAND = 0
CAN_MSG_TYPE_ACK = 1
CAN_MSG_TYPE_DATA_DONE_LOG = 3

CAN_SUB_TYPE_DONE = 0x01

CAN_ADDR_BROADCAST = 0x00
CAN_ADDR_CONDUCTOR = 0x10
CAN_ADDR_MOTION = 0x20
CAN_ADDR_FLUIDICS = 0x30

CAN_CMD_MOTOR_ROTATE = 0x0101
CAN_CMD_MOTOR_HOME = 0x0102
CAN_CMD_MOTOR_START_CONTINUOUS = 0x0103
CAN_CMD_MOTOR_STOP = 0x0104
CAN_CMD_PUMP_RUN_DURATION = 0x0201
CAN_CMD_PUMP_START = 0x0202
CAN_CMD_PUMP_STOP = 0x0203
CAN_CMD_VALVE_OPEN = 0x0204
CAN_CMD_VALVE_CLOSE = 0x0205
CAN_CMD_SRV_GET_INFO = 0xF001

CAN_COMMAND_NAMES = {
    CAN_CMD_MOTOR_ROTATE: "MOTOR_ROTATE",
    CAN_CMD_MOTOR_HOME: "MOTOR_HOME",
    CAN_CMD_MOTOR_START_CONTINUOUS: "MOTOR_START_CONTINUOUS",
    CAN_CMD_MOTOR_STOP: "MOTOR_STOP",
    CAN_CMD_PUMP_RUN_DURATION: "PUMP_RUN_DURATION",
    CAN_CMD_PUMP_START: "PUMP_START",
    CAN_CMD_PUMP_STOP: "PUMP_STOP",
    CAN_CMD_VALVE_OPEN: "VALVE_OPEN",
    CAN_CMD_VALVE_CLOSE: "VALVE_CLOSE",
    CAN_CMD_SRV_GET_INFO: "SRV_GET_INFO",
}

SUPPORTED_COMMANDS_BY_NODE = {
    CAN_ADDR_MOTION: {
        CAN_CMD_SRV_GET_INFO,
        CAN_CMD_MOTOR_ROTATE,
        CAN_CMD_MOTOR_HOME,
        CAN_CMD_MOTOR_START_CONTINUOUS,
        CAN_CMD_MOTOR_STOP,
    },
    CAN_ADDR_FLUIDICS: {
        CAN_CMD_SRV_GET_INFO,
        CAN_CMD_PUMP_RUN_DURATION,
        CAN_CMD_PUMP_START,
        CAN_CMD_PUMP_STOP,
        CAN_CMD_VALVE_OPEN,
        CAN_CMD_VALVE_CLOSE,
    },
}

CAN_LOG_RE = re.compile(r"\b(?P<id>[0-9A-Fa-f]{3,8})#(?P<data>[0-9A-Fa-f]*)\b")
CAN_DUMP_RE = re.compile(
    r"\b(?P<id>[0-9A-Fa-f]{3,8})\s+\[(?P<dlc>\d+)\]\s+"
    r"(?P<data>(?:[0-9A-Fa-f]{2}\s*){0,64})"
)

# Fake Motion board (NodeID 0x20) -> Conductor (NodeID 0x10).
# Current firmware updates inventory on DONE for 0xF001, not on DATA.
DISCOVERY_DONE = "07102000#0101F00000000000"

# DONE for HOME_MOTOR (0x0102), first Z channel (ch=1), then XY channel (ch=0).
HOME_Z_DONE = "07102000#0102010100000000"
HOME_XY_DONE = "07102000#0102010000000000"


def parse_int(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid integer: {value}") from exc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="USB + CAN fake-executor test for STM32H723 Conductor",
    )
    parser.add_argument(
        "-s",
        "--serial",
        default="/dev/ttyACM0",
        help="USB CDC serial port of the Conductor, default: /dev/ttyACM0",
    )
    parser.add_argument(
        "-c",
        "--can",
        dest="can_iface",
        default="can0",
        help="SocketCAN interface, default: can0",
    )
    parser.add_argument(
        "-m",
        "--mask",
        type=parse_int,
        default=0x01,
        help="INIT module mask. Use 0x01 for the smallest current INIT path, default: 0x01",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=8.0,
        help="Timeout for each USB wait stage, seconds, default: 8",
    )
    parser.add_argument(
        "--step-delay",
        type=float,
        default=0.5,
        help="Delay between fake CAN DONE frames, seconds, default: 0.5",
    )
    parser.add_argument(
        "--discovery-repeat",
        type=int,
        default=3,
        help="How many fake discovery DONE frames to send before INIT, default: 3",
    )
    parser.add_argument(
        "--no-auto-can",
        action="store_true",
        help="Do not run cansend automatically; print the commands instead",
    )
    parser.add_argument(
        "--skip-status",
        action="store_true",
        help="Skip GET_STATUS after INIT",
    )
    parser.add_argument(
        "--can-only-responder",
        action="store_true",
        help="Do not use USB. Listen to CAN and auto-reply as fake Motion node 0x20.",
    )
    parser.add_argument(
        "--stdin",
        action="store_true",
        help="For --can-only-responder: read candump lines from stdin instead of starting candump.",
    )
    parser.add_argument(
        "--ack-delay",
        type=float,
        default=0.0,
        help="For --can-only-responder: delay before ACK, seconds, default: 0",
    )
    parser.add_argument(
        "--done-delay",
        type=float,
        default=0.05,
        help="For --can-only-responder: delay between ACK and DONE, seconds, default: 0.05",
    )
    parser.add_argument(
        "--no-ack",
        action="store_true",
        help="For --can-only-responder: send DONE only. Current Conductor advances on DONE.",
    )
    parser.add_argument(
        "--motion-only",
        action="store_true",
        help="For --can-only-responder: only emulate Motion node 0x20, not Fluidics 0x30.",
    )
    return parser.parse_args()


def load_usb_proto():
    try:
        import test_main_processes as module
    except ImportError as exc:
        print(f"ERROR: cannot import test_main_processes.py from {SCRIPT_DIR}: {exc}")
        print("If the missing module is 'serial', install it in the active environment:")
        print("  python3 -m pip install pyserial")
        sys.exit(2)
    return module


def clear_usb_queue() -> None:
    while True:
        try:
            usb_proto.received_messages_queue.get_nowait()
        except queue.Empty:
            return


def format_binary(msg: dict) -> str:
    content = msg["content"]
    command = content["command_code"]
    response_type = content["response_type"]
    raw = content["raw_packet"].hex(" ")
    type_name = {
        0x00: "NACK",
        0x01: "ACK",
        0x02: "DONE",
        0x03: "DATA",
        0x04: "ERROR",
    }.get(response_type, f"TYPE_0x{response_type:02X}")

    data = content["status_or_data"]
    if response_type in (0x00, 0x01, 0x02, 0x04) and len(data) >= 2:
        status = int.from_bytes(data[:2], "big")
        return f"USB <- {type_name} cmd=0x{command:04X} status=0x{status:04X} raw={raw}"

    if response_type == 0x03 and len(data) >= 3:
        embedded_type = data[0]
        status = int.from_bytes(data[1:3], "big")
        payload = data[3:]
        return (
            f"USB <- DATA cmd=0x{command:04X} embedded=0x{embedded_type:02X} "
            f"status=0x{status:04X} payload={payload.hex(' ')} raw={raw}"
        )

    return f"USB <- {type_name} cmd=0x{command:04X} data={data.hex(' ')} raw={raw}"


def print_usb_msg(msg: dict) -> None:
    if msg["type"] == "text":
        print(f"USB <- TEXT: {msg['content']}")
    elif msg["type"] == "binary":
        print(format_binary(msg))
    else:
        print(f"USB <- {msg}")


def wait_for_usb_response(
    command_code: int,
    response_types: set[int],
    timeout_s: float,
) -> dict | None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            msg = usb_proto.received_messages_queue.get(timeout=0.1)
        except queue.Empty:
            continue

        print_usb_msg(msg)

        if msg["type"] != "binary":
            continue

        content = msg["content"]
        if (
            content["command_code"] == command_code
            and content["response_type"] in response_types
        ):
            return msg

    expected = ", ".join(f"0x{x:02X}" for x in sorted(response_types))
    print(f"TIMEOUT: no USB response cmd=0x{command_code:04X}, type in {{{expected}}}")
    return None


def send_can_frame(args: argparse.Namespace, frame: str, label: str) -> bool:
    command = ["cansend", args.can_iface, frame]
    printable = " ".join(command)

    if args.no_auto_can:
        print(f"MANUAL CAN ({label}): {printable}")
        return True

    if shutil.which("cansend") is None:
        print("ERROR: cansend not found. Install can-utils or rerun with --no-auto-can.")
        print(f"MANUAL CAN ({label}): {printable}")
        return False

    print(f"CAN -> {label}: {printable}")
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode != 0:
        print(f"ERROR: cansend failed with code {result.returncode}")
        if result.stdout:
            print(result.stdout.strip())
        if result.stderr:
            print(result.stderr.strip())
        return False
    return True


def can_build_id(priority: int, msg_type: int, dst_addr: int, src_addr: int) -> int:
    return (
        ((priority & 0x07) << 26)
        | ((msg_type & 0x03) << 24)
        | ((dst_addr & 0xFF) << 16)
        | ((src_addr & 0xFF) << 8)
    )


def can_get_msg_type(can_id: int) -> int:
    return (can_id >> 24) & 0x03


def can_get_dst_addr(can_id: int) -> int:
    return (can_id >> 16) & 0xFF


def can_get_src_addr(can_id: int) -> int:
    return (can_id >> 8) & 0xFF


def parse_candump_line(line: str) -> tuple[int, bytes] | None:
    match = CAN_LOG_RE.search(line)
    if match:
        data_hex = match.group("data")
        if len(data_hex) % 2 != 0:
            return None
        return int(match.group("id"), 16), bytes.fromhex(data_hex)

    match = CAN_DUMP_RE.search(line)
    if not match:
        return None

    dlc = int(match.group("dlc"))
    data_hex = "".join(match.group("data").split())
    if len(data_hex) % 2 != 0:
        return None
    data = bytes.fromhex(data_hex)
    if len(data) != dlc:
        return None
    return int(match.group("id"), 16), data


def format_can_command_name(command_code: int) -> str:
    name = CAN_COMMAND_NAMES.get(command_code)
    return name if name is not None else f"CMD_0x{command_code:04X}"


def node_name(node_id: int) -> str:
    return {
        CAN_ADDR_MOTION: "Motion",
        CAN_ADDR_FLUIDICS: "Fluidics",
    }.get(node_id, f"Node 0x{node_id:02X}")


def send_executor_ack(args: argparse.Namespace, source_addr: int, command_code: int) -> bool:
    ack_id = can_build_id(
        CAN_PRIORITY_NORMAL,
        CAN_MSG_TYPE_ACK,
        CAN_ADDR_CONDUCTOR,
        source_addr,
    )
    payload = command_code.to_bytes(2, "little") + b"\x00\x00\x00\x00\x00\x00"
    frame = f"{ack_id:08X}#{payload.hex().upper()}"
    return send_can_frame(
        args,
        frame,
        f"{node_name(source_addr)} ACK {format_can_command_name(command_code)}",
    )


def send_executor_done(
    args: argparse.Namespace,
    source_addr: int,
    command_code: int,
    channel: int,
) -> bool:
    done_id = can_build_id(
        CAN_PRIORITY_NORMAL,
        CAN_MSG_TYPE_DATA_DONE_LOG,
        CAN_ADDR_CONDUCTOR,
        source_addr,
    )
    payload = (
        bytes([CAN_SUB_TYPE_DONE])
        + command_code.to_bytes(2, "little")
        + bytes([channel & 0xFF])
        + b"\x00\x00\x00\x00"
    )
    frame = f"{done_id:08X}#{payload.hex().upper()}"
    return send_can_frame(
        args,
        frame,
        f"{node_name(source_addr)} DONE {format_can_command_name(command_code)} ch={channel}",
    )


def send_fake_executor_reply(
    args: argparse.Namespace,
    source_addr: int,
    command_code: int,
    channel: int,
) -> bool:
    if command_code not in SUPPORTED_COMMANDS_BY_NODE.get(source_addr, set()):
        print(
            f"CAN responder: {node_name(source_addr)} has no fake reply for "
            f"{format_can_command_name(command_code)}"
        )
        return True

    if not args.no_ack:
        if args.ack_delay > 0:
            time.sleep(args.ack_delay)
        if not send_executor_ack(args, source_addr, command_code):
            return False

    if args.done_delay > 0:
        time.sleep(args.done_delay)
    return send_executor_done(args, source_addr, command_code, channel)


def handle_can_command(args: argparse.Namespace, can_id: int, data: bytes) -> bool:
    if len(data) != 8:
        print(f"CAN <- ignored: id=0x{can_id:08X}, DLC={len(data)}")
        return True

    msg_type = can_get_msg_type(can_id)
    dst_addr = can_get_dst_addr(can_id)
    src_addr = can_get_src_addr(can_id)

    fake_nodes = [CAN_ADDR_MOTION]
    if not args.motion_only:
        fake_nodes.append(CAN_ADDR_FLUIDICS)

    if msg_type != CAN_MSG_TYPE_COMMAND or src_addr != CAN_ADDR_CONDUCTOR:
        return True
    if dst_addr not in (CAN_ADDR_BROADCAST, *fake_nodes):
        return True

    command_code = int.from_bytes(data[0:2], "little")
    channel = data[2]
    parameter = int.from_bytes(data[3:7], "little")
    name = format_can_command_name(command_code)
    print(
        f"CAN <- Conductor cmd {name} dst=0x{dst_addr:02X} "
        f"ch={channel} param=0x{parameter:08X}"
    )

    if dst_addr == CAN_ADDR_BROADCAST:
        if command_code != CAN_CMD_SRV_GET_INFO:
            print("CAN responder: broadcast command ignored")
            return True
        for fake_node in fake_nodes:
            if not send_fake_executor_reply(args, fake_node, command_code, channel):
                return False
        return True

    if not send_fake_executor_reply(args, dst_addr, command_code, channel):
            return False
    return True


def run_can_only_responder(args: argparse.Namespace) -> int:
    nodes = [f"0x{CAN_ADDR_MOTION:02X} Motion"]
    if not args.motion_only:
        nodes.append(f"0x{CAN_ADDR_FLUIDICS:02X} Fluidics")

    print("CAN-only fake executor responder")
    print(f"iface={args.can_iface}, nodes={', '.join(nodes)}")
    print("Restart the Conductor now. Press Ctrl+C to stop.")

    if not args.no_auto_can and shutil.which("cansend") is None:
        print("ERROR: cansend not found. Install can-utils or use --no-auto-can.")
        return 2

    process = None
    if args.stdin:
        line_source = sys.stdin
    else:
        if shutil.which("candump") is None:
            print("ERROR: candump not found. Install can-utils.")
            return 2
        command = ["candump", "-L", args.can_iface]
        print(f"Starting: {' '.join(command)}")
        process = subprocess.Popen(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=1,
        )
        if process.stdout is None:
            print("ERROR: failed to read candump stdout.")
            return 2
        line_source = process.stdout

    try:
        for line in line_source:
            parsed = parse_candump_line(line)
            if parsed is None:
                continue
            can_id, data = parsed
            if not handle_can_command(args, can_id, data):
                return 1
    except KeyboardInterrupt:
        print("\nInterrupted")
        return 130
    finally:
        if process is not None:
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()

    return 0


def send_usb_command(command_code: int, params: bytes = b"") -> None:
    packet = usb_proto.build_command(command_code, params)
    print(f"USB -> cmd=0x{command_code:04X}: {packet.hex(' ')}")
    usb_proto.ser.write(packet)


def start_usb(serial_port: str) -> threading.Thread:
    usb_proto.SERIAL_PORT = serial_port
    usb_proto.stop_listening_event.clear()

    print(f"Opening USB serial: {serial_port}")
    usb_proto.ser = usb_proto.serial.Serial(serial_port, usb_proto.BAUD_RATE, timeout=0)
    time.sleep(1.0)
    usb_proto.ser.reset_input_buffer()
    usb_proto.ser.reset_output_buffer()
    clear_usb_queue()

    listener = threading.Thread(target=usb_proto.listen_serial_port, daemon=True)
    listener.start()
    return listener


def stop_usb(listener: threading.Thread | None) -> None:
    usb_proto.stop_listening_event.set()
    if listener is not None:
        listener.join(timeout=2)
    if usb_proto.ser is not None and usb_proto.ser.is_open:
        usb_proto.ser.close()
        print("USB serial closed")


def run_init_test(args: argparse.Namespace) -> bool:
    print("\n=== Stage 1: fake Motion board discovery ===")
    for idx in range(max(args.discovery_repeat, 0)):
        if not send_can_frame(args, DISCOVERY_DONE, f"discovery DONE {idx + 1}"):
            return False
        time.sleep(0.15)

    print("\n=== Stage 2: USB INIT ===")
    mask = args.mask & 0xFF
    send_usb_command(CMD_INIT, bytes([mask]))

    ack = wait_for_usb_response(CMD_INIT, {0x00, 0x01, 0x04}, args.timeout)
    if ack is None:
        return False

    ack_type = ack["content"]["response_type"]
    if ack_type != 0x01:
        print("FAIL: INIT was not ACKed by the Conductor.")
        return False

    print("\n=== Stage 3: fake executor completes INIT steps ===")
    time.sleep(args.step_delay)
    if not send_can_frame(args, HOME_Z_DONE, "HOME_MOTOR DONE ch=1"):
        return False
    time.sleep(args.step_delay)
    if not send_can_frame(args, HOME_XY_DONE, "HOME_MOTOR DONE ch=0"):
        return False

    done = wait_for_usb_response(CMD_INIT, {0x02, 0x04}, args.timeout)
    if done is None:
        return False

    done_type = done["content"]["response_type"]
    status_data = done["content"]["status_or_data"]
    status = int.from_bytes(status_data[:2], "big") if len(status_data) >= 2 else 0xFFFF

    if done_type == 0x02 and status == 0x0000:
        print("PASS: INIT completed through fake CAN executor responses.")
        return True

    print(f"FAIL: INIT ended with response type 0x{done_type:02X}, status 0x{status:04X}.")
    return False


def run_status_test(args: argparse.Namespace) -> bool:
    print("\n=== Stage 4: USB GET_STATUS ===")
    send_usb_command(CMD_GET_STATUS)

    ack = wait_for_usb_response(CMD_GET_STATUS, {0x00, 0x01, 0x04}, args.timeout)
    if ack is None or ack["content"]["response_type"] != 0x01:
        print("FAIL: GET_STATUS was not ACKed.")
        return False

    got_data = False
    got_done = False
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline and not (got_data and got_done):
        try:
            msg = usb_proto.received_messages_queue.get(timeout=0.1)
        except queue.Empty:
            continue

        print_usb_msg(msg)
        if msg["type"] != "binary":
            continue

        content = msg["content"]
        if content["command_code"] != CMD_GET_STATUS:
            continue
        if content["response_type"] == 0x03:
            got_data = True
        elif content["response_type"] == 0x02:
            got_done = True

    if got_data and got_done:
        print("PASS: GET_STATUS returned DATA and DONE.")
        return True

    print("FAIL: GET_STATUS did not return both DATA and DONE.")
    return False


def main() -> int:
    global usb_proto
    args = parse_args()

    if args.can_only_responder:
        return run_can_only_responder(args)

    usb_proto = load_usb_proto()
    listener: threading.Thread | None = None

    print("CAN/USB Conductor smoke test")
    print(f"serial={args.serial}, can={args.can_iface}, init_mask=0x{args.mask & 0xFF:02X}")
    print("Tip: run `candump -x -t a can0` in another terminal to see TX/RX marks.")

    try:
        listener = start_usb(args.serial)
        init_ok = run_init_test(args)
        status_ok = True
        if init_ok and not args.skip_status:
            status_ok = run_status_test(args)
        return 0 if init_ok and status_ok else 1
    except usb_proto.serial.SerialException as exc:
        print(f"ERROR: cannot open serial port {args.serial}: {exc}")
        print("Check with: ls /dev/ttyACM*")
        return 2
    except KeyboardInterrupt:
        print("\nInterrupted")
        return 130
    finally:
        stop_usb(listener)


if __name__ == "__main__":
    sys.exit(main())
