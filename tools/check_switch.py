#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial==3.5"]
# ///
"""Bounded app-switch round trips over USB using QA builds of the launcher and an app.

Requires the launcher QA command TEST_BOOT and the app QA command that opens the launcher
(Sparklet: TEST_OPEN_LAUNCHER). Opening USB may reset the board; one session is kept open.
"""

import argparse
import json
import re
import sys
import time
from pathlib import Path

import serial
from serial.tools import list_ports

BOARD_SERIAL = "D4:05:92:B9:04:28"
LAUNCHER_BOOT = re.compile(
    rb"launcher: BOOT version=\S+ reset=(\d+) reason=(\w+) slot=(\d+)"
)
APP_BOOT = {"sparklet": re.compile(rb"sparkdash: BOOT version=\S+")}
APP_OPEN = {"sparklet": b"TEST_OPEN_LAUNCHER"}


class Session:
    def __init__(self, port, raw):
        self.conn = serial.Serial(port=None, baudrate=115200, timeout=0.1)
        self.conn.dtr = self.conn.rts = False
        self.conn.port = port
        self.conn.open()
        self.buffer = b""
        self.raw = raw

    def wait(self, pattern, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            data = self.conn.read(4096)
            if data:
                self.raw.write(data)
                self.buffer += data
                match = pattern.search(self.buffer)
                if match:
                    self.buffer = self.buffer[match.end() :]
                    return match
        raise TimeoutError(f"no match for {pattern.pattern!r} within {seconds} s")

    def send(self, line):
        self.conn.write(line + b"\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", default="sparklet", choices=sorted(APP_BOOT))
    parser.add_argument("--cycles", type=int, default=5)
    parser.add_argument(
        "--settle", type=float, default=6.0, help="seconds in each image"
    )
    parser.add_argument(
        "--output", type=Path, required=True, help="new JSON result file"
    )
    args = parser.parse_args()
    if args.output.exists():
        parser.error("refusing to overwrite results")
    ports = [
        p.device
        for p in list_ports.comports()
        if (p.vid, p.pid) == (0x303A, 0x1001) and p.serial_number == BOARD_SERIAL
    ]
    if len(ports) != 1:
        parser.error("expected exactly one known board")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    results = {"app": args.app, "cycles": [], "failures": []}
    with args.output.with_suffix(".raw").open("xb") as raw:
        session = Session(ports[0], raw)
        try:
            session.send(b"")
            first = session.wait(LAUNCHER_BOOT, 15)
            results["initial_reason"] = first.group(2).decode()
            for cycle in range(args.cycles):
                time.sleep(args.settle)
                started = time.monotonic()
                session.send(b"TEST_BOOT " + args.app.encode())
                session.wait(APP_BOOT[args.app], 15)
                to_app = time.monotonic() - started
                time.sleep(args.settle)
                started = time.monotonic()
                session.send(APP_OPEN[args.app])
                back = session.wait(LAUNCHER_BOOT, 15)
                to_launcher = time.monotonic() - started
                entry = {
                    "cycle": cycle + 1,
                    "to_app_s": round(to_app, 2),
                    "to_launcher_s": round(to_launcher, 2),
                    "launcher_reset_reason": int(back.group(1)),
                    "launcher_reason": back.group(2).decode(),
                    "launcher_slot": int(back.group(3)),
                }
                results["cycles"].append(entry)
                if (
                    entry["launcher_reason"] != "requested"
                    or entry["launcher_slot"] != 0
                ):
                    results["failures"].append(f"cycle {cycle + 1}: {entry}")
                print(json.dumps(entry), flush=True)
        except TimeoutError as error:
            results["failures"].append(str(error))
        finally:
            session.conn.close()
    args.output.write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps({"failures": results["failures"], "result": str(args.output)}))
    return 1 if results["failures"] else 0


if __name__ == "__main__":
    sys.exit(main())
