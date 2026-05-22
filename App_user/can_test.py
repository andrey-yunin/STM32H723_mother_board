#!/usr/bin/env python3
"""
Smoke test for the current Conductor + CANable bench.

What it checks:
1. USB command path works by sending INIT (0x1002).
2. Conductor receives fake executor frames from CANable.
3. Fake executors follow the current DDS-240 service chain:
   F001 GET_DEVICE_INFO -> F004 GET_UID -> F007 GET_STATUS.
4. Thermo direct commands 0x9010/0x9011 can be exercised through the
   executor-backed Host direct path.

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
CMD_SENSOR_GET_ALL_TEMPS = 0x9010
CMD_SENSOR_GET_TEMP = 0x9011

CAN_PRIORITY_NORMAL = 1
CAN_MSG_TYPE_COMMAND = 0
CAN_MSG_TYPE_ACK = 1
CAN_MSG_TYPE_NACK = 2
CAN_MSG_TYPE_DATA_DONE_LOG = 3

CAN_SUB_TYPE_DONE = 0x01
CAN_SUB_TYPE_DATA = 0x02

CAN_ADDR_BROADCAST = 0x00
CAN_ADDR_CONDUCTOR = 0x10
CAN_ADDR_MOTION = 0x20
CAN_ADDR_FLUIDICS = 0x30
CAN_ADDR_THERMO = 0x40

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
CAN_CMD_SRV_GET_UID = 0xF004
CAN_CMD_SRV_GET_STATUS = 0xF007
CAN_CMD_THERMO_GET_ALL_TEMPS = 0x9010
CAN_CMD_THERMO_GET_TEMP = 0x9011

CAN_ERR_INVALID_PARAM = 0x0006

SERVICE_STATUS_METRIC_IDS = tuple(range(0x0001, 0x0013))

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
    CAN_CMD_SRV_GET_UID: "SRV_GET_UID",
    CAN_CMD_SRV_GET_STATUS: "SRV_GET_STATUS",
    CAN_CMD_THERMO_GET_ALL_TEMPS: "THERMO_GET_ALL_TEMPS",
    CAN_CMD_THERMO_GET_TEMP: "THERMO_GET_TEMP",
}

SUPPORTED_COMMANDS_BY_NODE = {
    CAN_ADDR_MOTION: {
        CAN_CMD_SRV_GET_INFO,
        CAN_CMD_SRV_GET_UID,
        CAN_CMD_SRV_GET_STATUS,
        CAN_CMD_MOTOR_ROTATE,
        CAN_CMD_MOTOR_HOME,
        CAN_CMD_MOTOR_START_CONTINUOUS,
        CAN_CMD_MOTOR_STOP,
    },
    CAN_ADDR_FLUIDICS: {
        CAN_CMD_SRV_GET_INFO,
        CAN_CMD_SRV_GET_UID,
        CAN_CMD_SRV_GET_STATUS,
        CAN_CMD_PUMP_RUN_DURATION,
        CAN_CMD_PUMP_START,
        CAN_CMD_PUMP_STOP,
        CAN_CMD_VALVE_OPEN,
        CAN_CMD_VALVE_CLOSE,
    },
    CAN_ADDR_THERMO: {
        CAN_CMD_SRV_GET_INFO,
        CAN_CMD_SRV_GET_UID,
        CAN_CMD_SRV_GET_STATUS,
        CAN_CMD_THERMO_GET_ALL_TEMPS,
        CAN_CMD_THERMO_GET_TEMP,
    },
}

DEVICE_PROFILES = {
    CAN_ADDR_MOTION: {
        "name": "Motion",
        "device_type": 0x01,
        "fw": (1, 0),
        "channels": 8,
        "uid": bytes.fromhex("20 01 00 00 20 02 00 00 20 03 00 00"),
    },
    CAN_ADDR_FLUIDICS: {
        "name": "Fluidics",
        "device_type": 0x03,
        "fw": (1, 0),
        "channels": 16,
        "uid": bytes.fromhex("30 01 00 00 30 02 00 00 30 03 00 00"),
    },
    CAN_ADDR_THERMO: {
        "name": "Thermo",
        "device_type": 0x02,
        "fw": (1, 0),
        "channels": 8,
        "uid": bytes.fromhex("40 01 00 00 40 02 00 00 40 03 00 00"),
    },
}

THERMO_FAKE_TEMPS_DECI_C = (246, 247, 248, 249, 250, 251, 252, 253)

CAN_LOG_RE = re.compile(r"\b(?P<id>[0-9A-Fa-f]{3,8})#(?P<data>[0-9A-Fa-f]*)\b")
CAN_DUMP_RE = re.compile(
    r"\b(?P<id>[0-9A-Fa-f]{3,8})\s+\[(?P<dlc>\d+)\]\s+"
    r"(?P<data>(?:[0-9A-Fa-f]{2}\s*){0,64})"
)

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
        "--test-thermo",
        action="store_true",
        help="After INIT, test Host direct Thermo commands 0x9011 and 0x9010.",
    )
    parser.add_argument(
        "--can-only-responder",
        action="store_true",
        help="Do not use USB. Listen to CAN and auto-reply as fake DDS-240 executors.",
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
        default=0.0,
        help="For --can-only-responder: delay between ACK and DONE, seconds, default: 0",
    )
    parser.add_argument(
        "--no-ack",
        action="store_true",
        help="Fault-injection mode: omit ACK. Current Conductor should reject or timeout this flow.",
    )
    parser.add_argument(
        "--motion-only",
        action="store_true",
        help="Only emulate Motion node 0x20 among Motion/Fluidics nodes.",
    )
    parser.add_argument(
        "--no-thermo",
        action="store_true",
        help="Do not emulate Thermo node 0x40.",
    )
    parser.add_argument(
        "--nack-command",
        type=parse_int,
        default=None,
        help="Fake a NACK without DONE for the selected low-level CAN command.",
    )
    parser.add_argument(
        "--nack-code",
        type=parse_int,
        default=CAN_ERR_INVALID_PARAM,
        help="NACK code used with --nack-command, default: 0x0006.",
    )
    parser.add_argument(
        "--ack-only-command",
        type=parse_int,
        default=None,
        help="Send ACK only for the selected low-level command, then wait for Conductor timeout/recovery.",
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
    if module.serial is None:
        print("ERROR: pyserial is not installed in the active Python environment.")
        print("Activate App_user/.venv or install it:")
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
        CAN_ADDR_THERMO: "Thermo",
    }.get(node_id, f"Node 0x{node_id:02X}")


def get_fake_nodes(args: argparse.Namespace) -> list[int]:
    nodes = [CAN_ADDR_MOTION]
    if not args.motion_only:
        nodes.append(CAN_ADDR_FLUIDICS)
    if not args.no_thermo:
        nodes.append(CAN_ADDR_THERMO)
    return nodes


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


def send_executor_nack(
    args: argparse.Namespace,
    source_addr: int,
    command_code: int,
    error_code: int,
) -> bool:
    nack_id = can_build_id(
        CAN_PRIORITY_NORMAL,
        CAN_MSG_TYPE_NACK,
        CAN_ADDR_CONDUCTOR,
        source_addr,
    )
    payload = (
        command_code.to_bytes(2, "little")
        + (error_code & 0xFFFF).to_bytes(2, "little")
        + b"\x00\x00\x00\x00"
    )
    frame = f"{nack_id:08X}#{payload.hex().upper()}"
    return send_can_frame(
        args,
        frame,
        f"{node_name(source_addr)} NACK {format_can_command_name(command_code)} "
        f"err=0x{error_code & 0xFFFF:04X}",
    )


def send_executor_data(
    args: argparse.Namespace,
    source_addr: int,
    data_info: int,
    payload: bytes,
    label: str,
) -> bool:
    data_id = can_build_id(
        CAN_PRIORITY_NORMAL,
        CAN_MSG_TYPE_DATA_DONE_LOG,
        CAN_ADDR_CONDUCTOR,
        source_addr,
    )
    payload6 = payload[:6].ljust(6, b"\x00")
    frame_payload = bytes([CAN_SUB_TYPE_DATA, data_info & 0xFF]) + payload6
    frame = f"{data_id:08X}#{frame_payload.hex().upper()}"
    return send_can_frame(args, frame, label)


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


def send_service_get_info_sequence(args: argparse.Namespace, source_addr: int) -> bool:
    profile = DEVICE_PROFILES[source_addr]
    uid = profile["uid"]
    fw_major, fw_minor = profile["fw"]

    frames = (
        bytes([profile["device_type"], fw_major, fw_minor, profile["channels"]]) + uid[0:2],
        uid[2:8],
        uid[8:12],
    )

    for seq, payload in enumerate(frames):
        if not send_executor_data(
            args,
            source_addr,
            seq,
            payload,
            f"{node_name(source_addr)} DATA F001 seq={seq}",
        ):
            return False
    return send_executor_done(args, source_addr, CAN_CMD_SRV_GET_INFO, 0)


def send_service_get_uid_sequence(args: argparse.Namespace, source_addr: int) -> bool:
    uid = DEVICE_PROFILES[source_addr]["uid"]
    for seq, offset in enumerate((0, 6)):
        if not send_executor_data(
            args,
            source_addr,
            seq,
            uid[offset : offset + 6],
            f"{node_name(source_addr)} DATA F004 seq={seq}",
        ):
            return False
    return send_executor_done(args, source_addr, CAN_CMD_SRV_GET_UID, 0)


def send_service_get_status_sequence(args: argparse.Namespace, source_addr: int) -> bool:
    for seq, metric_id in enumerate(SERVICE_STATUS_METRIC_IDS):
        payload = metric_id.to_bytes(2, "little") + (0).to_bytes(4, "little")
        if not send_executor_data(
            args,
            source_addr,
            seq,
            payload,
            f"{node_name(source_addr)} DATA F007 metric=0x{metric_id:04X}",
        ):
            return False
    return send_executor_done(args, source_addr, CAN_CMD_SRV_GET_STATUS, 0)


def send_thermo_get_temp_sequence(args: argparse.Namespace, source_addr: int, channel: int) -> bool:
    channel = channel & 0x07
    temperature = THERMO_FAKE_TEMPS_DECI_C[channel]
    payload = int(temperature).to_bytes(2, "little", signed=True)
    if not send_executor_data(
        args,
        source_addr,
        channel,
        payload,
        f"{node_name(source_addr)} DATA 0x9011 ch={channel}",
    ):
        return False
    return send_executor_done(args, source_addr, CAN_CMD_THERMO_GET_TEMP, channel)


def send_thermo_get_all_sequence(args: argparse.Namespace, source_addr: int) -> bool:
    for channel, temperature in enumerate(THERMO_FAKE_TEMPS_DECI_C):
        payload = (
            bytes([channel])
            + int(temperature).to_bytes(2, "little", signed=True)
            + b"\x00\x00\x00"
        )
        if not send_executor_data(
            args,
            source_addr,
            channel,
            payload,
            f"{node_name(source_addr)} DATA 0x9010 ch={channel}",
        ):
            return False
    return send_executor_done(args, source_addr, CAN_CMD_THERMO_GET_ALL_TEMPS, 0)


def send_command_specific_data(
    args: argparse.Namespace,
    source_addr: int,
    command_code: int,
    channel: int,
) -> bool:
    if command_code == CAN_CMD_SRV_GET_INFO:
        return send_service_get_info_sequence(args, source_addr)
    if command_code == CAN_CMD_SRV_GET_UID:
        return send_service_get_uid_sequence(args, source_addr)
    if command_code == CAN_CMD_SRV_GET_STATUS:
        return send_service_get_status_sequence(args, source_addr)
    if command_code == CAN_CMD_THERMO_GET_TEMP:
        return send_thermo_get_temp_sequence(args, source_addr, channel)
    if command_code == CAN_CMD_THERMO_GET_ALL_TEMPS:
        return send_thermo_get_all_sequence(args, source_addr)
    return send_executor_done(args, source_addr, command_code, channel)


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

    if args.nack_command is not None and command_code == args.nack_command:
        return send_executor_nack(args, source_addr, command_code, args.nack_code)

    if not args.no_ack:
        if args.ack_delay > 0:
            time.sleep(args.ack_delay)
        if not send_executor_ack(args, source_addr, command_code):
            return False

    if args.ack_only_command is not None and command_code == args.ack_only_command:
        print(
            f"CAN responder: ACK-only fault injection for "
            f"{format_can_command_name(command_code)}"
        )
        return True

    if args.done_delay > 0:
        time.sleep(args.done_delay)
    return send_command_specific_data(args, source_addr, command_code, channel)


def handle_can_command(args: argparse.Namespace, can_id: int, data: bytes) -> bool:
    if len(data) != 8:
        print(f"CAN <- ignored: id=0x{can_id:08X}, DLC={len(data)}")
        return True

    msg_type = can_get_msg_type(can_id)
    dst_addr = can_get_dst_addr(can_id)
    src_addr = can_get_src_addr(can_id)

    fake_nodes = get_fake_nodes(args)

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
    nodes = [f"0x{node:02X} {node_name(node)}" for node in get_fake_nodes(args)]

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
    print("\n=== Stage 1: service discovery prerequisite ===")
    print(
        "Discovery is now F001 -> F004 -> F007. Use real executors or run "
        "`python3 App_user/can_test.py --can-only-responder` while resetting the Conductor."
    )

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

    print("\n=== Stage 3: executor-backed INIT completion ===")
    print("Waiting for real/fake executor ACK/DATA/DONE on CAN routes.")

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


def run_host_data_done_command(
    args: argparse.Namespace,
    command_code: int,
    params: bytes,
    label: str,
) -> bool:
    print(f"\n=== USB {label} ===")
    send_usb_command(command_code, params)

    ack = wait_for_usb_response(command_code, {0x00, 0x01, 0x04}, args.timeout)
    if ack is None or ack["content"]["response_type"] != 0x01:
        print(f"FAIL: {label} was not ACKed.")
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
        if content["command_code"] != command_code:
            continue
        if content["response_type"] == 0x03:
            got_data = True
        elif content["response_type"] == 0x02:
            status_data = content["status_or_data"]
            status = int.from_bytes(status_data[:2], "big") if len(status_data) >= 2 else 0xFFFF
            if status != 0x0000:
                print(f"FAIL: {label} DONE status=0x{status:04X}.")
                return False
            got_done = True

    if got_data and got_done:
        print(f"PASS: {label} returned DATA and DONE.")
        return True

    print(f"FAIL: {label} did not return both DATA and DONE.")
    return False


def run_thermo_tests(args: argparse.Namespace) -> bool:
    if args.no_thermo:
        print("FAIL: --test-thermo requires Thermo responder; remove --no-thermo.")
        return False

    if not run_host_data_done_command(
        args,
        CMD_SENSOR_GET_TEMP,
        b"\x01",
        "SENSOR_GET_TEMP 0x9011 sensor_id=1",
    ):
        return False

    return run_host_data_done_command(
        args,
        CMD_SENSOR_GET_ALL_TEMPS,
        b"",
        "SENSOR_GET_ALL_TEMPS 0x9010",
    )


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
        thermo_ok = True
        if init_ok and status_ok and args.test_thermo:
            thermo_ok = run_thermo_tests(args)
        return 0 if init_ok and status_ok and thermo_ok else 1
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
