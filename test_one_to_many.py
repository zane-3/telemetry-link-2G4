#!/usr/bin/env python3
"""
Serial test helper for telemetry-link-2G4 one-to-many mode.

Examples:
  python test_one_to_many.py --list
  python test_one_to_many.py --query COM3
  python test_one_to_many.py --master COM3 --slave1 COM4
  python test_one_to_many.py --master COM3 --slave1 COM4 --slave2 COM5 --configure
"""

import argparse
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


BAUDRATE = 115200
READ_GAP_S = 0.25
CFG_APPLY_WAIT_S = 4.0


def checksum(data: bytes) -> int:
    return sum(data) & 0xFF


def build_frame(target: int, src: int, seq: int, cmd: int, payload: bytes = b"") -> bytes:
    body_len = 4 + len(payload)
    frame = bytes([0xA5, body_len, target & 0xFF, src & 0xFF, seq & 0xFF, cmd & 0xFF]) + payload
    return frame + bytes([checksum(frame)])


def hex_bytes(data: bytes) -> str:
    if not data:
        return "(none)"
    return " ".join(f"{byte:02X}" for byte in data)


def read_for(ser: serial.Serial, seconds: float) -> bytes:
    deadline = time.monotonic() + seconds
    chunks = []
    while time.monotonic() < deadline:
        waiting = ser.in_waiting
        if waiting:
            chunks.append(ser.read(waiting))
            deadline = time.monotonic() + READ_GAP_S
        else:
            time.sleep(0.02)
    return b"".join(chunks)


def open_port(port: str) -> serial.Serial:
    return serial.Serial(port, BAUDRATE, timeout=0.05, write_timeout=1)


def enter_cfg_mode(ser: serial.Serial) -> bytes:
    ser.reset_input_buffer()
    ser.write(b"+++")
    ser.flush()
    return read_for(ser, 0.8)


def send_cfg(ser: serial.Serial, command: str, wait_s: float = 0.8) -> bytes:
    ser.write(command.encode("ascii") + b"\r\n")
    ser.flush()
    return read_for(ser, wait_s)


def query_config(port: str) -> bytes:
    with open_port(port) as ser:
        response = enter_cfg_mode(ser)
        response += send_cfg(ser, "CFG?")
        return response


def configure_device(port: str, role: str, local_id: int, remote_id: int = 1) -> None:
    role = role.upper()
    with open_port(port) as ser:
        print(f"\n[{port}] enter config mode")
        print_text(enter_cfg_mode(ser))
        commands = [
            f"CFG ROLE={role}",
            f"CFG ID={local_id}",
            f"CFG REMOTE={remote_id}",
            "CFG CH=24",
            "CFG FILTER=1",
            "CFG ACK=1",
            "CFG SAVE",
        ]
        for command in commands:
            print(f"[{port}] > {command}")
            print_text(send_cfg(ser, command))
            if command == "CFG SAVE":
                time.sleep(1.0)

        print(f"[{port}] waiting for reboot")
        time.sleep(CFG_APPLY_WAIT_S)
        print(f"[{port}] verify")
        print_text(enter_cfg_mode(ser) + send_cfg(ser, "CFG?"))


def print_text(data: bytes) -> None:
    if not data:
        print("  (no response)")
        return
    text = data.decode("utf-8", errors="replace").strip()
    if text:
        for line in text.splitlines():
            print(f"  {line}")
    else:
        print(f"  {hex_bytes(data)}")


def drain(*ports: serial.Serial) -> None:
    for ser in ports:
        ser.reset_input_buffer()


def expect(label: str, actual: bytes, expected: bytes | None = None, should_be_empty: bool = False) -> bool:
    print(f"{label}: {hex_bytes(actual)}")
    if should_be_empty:
        ok = actual == b""
    elif expected is None:
        ok = actual != b""
    else:
        ok = actual == expected
    print(f"  {'PASS' if ok else 'FAIL'}")
    return ok


def run_tests(master_port: str, slave1_port: str, slave2_port: str | None) -> int:
    ok = True
    ports = [open_port(master_port), open_port(slave1_port)]
    if slave2_port:
        ports.append(open_port(slave2_port))

    try:
        master = ports[0]
        slave1 = ports[1]
        slave2 = ports[2] if len(ports) > 2 else None
        drain(*ports)

        frame = build_frame(target=0xFF, src=0x01, seq=0x10, cmd=0x20)
        print(f"\nBroadcast frame: {hex_bytes(frame)}")
        master.write(frame)
        master.flush()
        time.sleep(0.6)
        ok &= expect("slave1 broadcast rx", read_for(slave1, 0.2), frame)
        if slave2:
            ok &= expect("slave2 broadcast rx", read_for(slave2, 0.2), frame)

        drain(*ports)
        frame = build_frame(target=0x01, src=0x01, seq=0x11, cmd=0x21)
        print(f"\nUnicast slave1 frame: {hex_bytes(frame)}")
        master.write(frame)
        master.flush()
        time.sleep(0.8)
        ok &= expect("slave1 unicast rx", read_for(slave1, 0.2), frame)
        if slave2:
            ok &= expect("slave2 should filter", read_for(slave2, 0.2), should_be_empty=True)
        ack = read_for(master, 0.4)
        ok &= expect("master ack from slave1", ack, build_frame(0x01, 0x01, 0x11, 0xA1, b"\x00"))

        if slave2:
            drain(*ports)
            frame = build_frame(target=0x02, src=0x01, seq=0x12, cmd=0x22)
            print(f"\nUnicast slave2 frame: {hex_bytes(frame)}")
            master.write(frame)
            master.flush()
            time.sleep(0.8)
            ok &= expect("slave1 should filter", read_for(slave1, 0.2), should_be_empty=True)
            ok &= expect("slave2 unicast rx", read_for(slave2, 0.2), frame)
            ack = read_for(master, 0.4)
            ok &= expect("master ack from slave2", ack, build_frame(0x01, 0x02, 0x12, 0xA2, b"\x00"))
    finally:
        for ser in ports:
            ser.close()

    return 0 if ok else 1


def list_serial_ports() -> int:
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return 1
    for port in ports:
        print(f"{port.device}\t{port.description}")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="telemetry-link-2G4 one-to-many serial tester")
    parser.add_argument("--list", action="store_true", help="list serial ports")
    parser.add_argument("--query", metavar="PORT", help="enter config mode and send CFG?")
    parser.add_argument("--master", help="master serial port")
    parser.add_argument("--slave1", help="slave 1 serial port")
    parser.add_argument("--slave2", help="slave 2 serial port")
    parser.add_argument("--configure", action="store_true", help="configure devices before testing")
    args = parser.parse_args(argv)

    if args.list:
        return list_serial_ports()

    if args.query:
        print_text(query_config(args.query))
        return 0

    if not args.master or not args.slave1:
        parser.error("--master and --slave1 are required unless --list or --query is used")

    if args.configure:
        configure_device(args.master, "MASTER", 1, 1)
        configure_device(args.slave1, "SLAVE", 1, 1)
        if args.slave2:
            configure_device(args.slave2, "SLAVE", 2, 1)

    return run_tests(args.master, args.slave1, args.slave2)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
