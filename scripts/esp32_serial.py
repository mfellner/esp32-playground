#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial==3.5"]
# ///
"""Enumerate USB serial devices or receive bounded ESP32 console output."""

import argparse
import math
from pathlib import Path
import sys
import time

import serial
from serial.tools import list_ports

BOARD_SERIAL = "D4:05:92:B9:04:28"


def positive_seconds(value):
    seconds = float(value)
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError("seconds must be finite and positive")
    return seconds


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list", help="enumerate ports without opening them")
    monitor = commands.add_parser("monitor", help="read only; opening may reboot the board")
    monitor.add_argument("--port", help="explicit port; otherwise select the known USB serial")
    monitor.add_argument("--seconds", type=positive_seconds, default=5.0)
    monitor.add_argument("--baud", type=int, default=115200)
    monitor.add_argument("--output", type=Path, help="new raw log file; refuses overwrite")
    args = parser.parse_args()
    ports = list(list_ports.comports())
    if args.command == "list":
        for port in sorted(ports, key=lambda p: p.device):
            usb_id = f"{port.vid:04x}:{port.pid:04x}" if port.vid is not None and port.pid is not None else "-"
            print(f"{port.device}\t{usb_id}\t{port.serial_number or '-'}\t{port.description}")
        return 0
    port = args.port
    if not port:
        matches = [p for p in ports if (p.vid, p.pid) == (0x303A, 0x1001)
                   and p.serial_number == BOARD_SERIAL]
        if len(matches) != 1:
            parser.error("expected exactly one known board; run list and use --port if needed")
        port = matches[0].device
    log = None
    connection = serial.Serial(port=None, baudrate=args.baud, timeout=0.25)
    try:
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            log = args.output.open("xb")
        connection.dtr = False
        connection.rts = False
        connection.port = port
        print(f"Opening {port} at {args.baud}; this may reboot the board.", file=sys.stderr)
        connection.open()
        deadline = time.monotonic() + args.seconds
        received = 0
        while time.monotonic() < deadline:
            data = connection.read(min(connection.in_waiting or 1, 4096))
            if data:
                received += len(data)
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
                if log:
                    log.write(data)
        print(f"\nReceived {received} bytes.", file=sys.stderr)
    finally:
        connection.close()
        if log:
            log.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except (serial.SerialException, OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
