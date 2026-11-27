#!/usr/bin/env python3
"""Exercise real USB HID keyboard and mouse input in the Nexus installer and desktop.

The test boots the supplied ISO or installed disk in QEMU, enumerates the USB
HID devices on xHCI, selects the USB mouse (not the emulated PS/2 fallback),
injects key/motion/button reports through QMP, and verifies that both the USB
HID decoder and the shared desktop input dispatcher observe them.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path


class QMP:
    def __init__(self, path: Path) -> None:
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(5)
        self.sock.connect(str(path))
        self.stream = self.sock.makefile("rwb", buffering=0)
        greeting = self._read()
        if "QMP" not in greeting:
            raise RuntimeError(f"unexpected QMP greeting: {greeting}")
        self.command("qmp_capabilities")

    def _read(self) -> dict:
        line = self.stream.readline()
        if not line:
            raise RuntimeError("QMP connection closed unexpectedly")
        try:
            return json.loads(line)
        except json.JSONDecodeError as exc:
            raise RuntimeError(f"invalid QMP response: {line!r}") from exc

    def command(self, name: str, arguments: dict | None = None) -> object:
        payload = {"execute": name}
        if arguments is not None:
            payload["arguments"] = arguments
        self.stream.write(json.dumps(payload).encode("utf-8") + b"\r\n")
        while True:
            response = self._read()
            if "event" in response:
                continue
            if "error" in response:
                raise RuntimeError(f"QMP {name} failed: {response['error']}")
            if "return" in response:
                return response["return"]

    def hmp(self, command: str) -> str:
        result = self.command(
            "human-monitor-command", {"command-line": command}
        )
        return str(result or "")

    def input(self, events: list[dict]) -> None:
        self.command("input-send-event", {"events": events})

    def close(self) -> None:
        try:
            self.stream.close()
        finally:
            self.sock.close()


def wait_for_log(path: Path, required: tuple[str, ...], timeout: int) -> str:
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        if path.is_file():
            last = path.read_text(encoding="utf-8", errors="replace")
            if all(item in last for item in required):
                return last
        time.sleep(0.25)
    missing = [item for item in required if item not in last]
    raise RuntimeError(
        "QEMU did not produce required boot/input markers: "
        + ", ".join(missing) + "\n--- serial log ---\n" + last[-12000:]
    )


def wait_for_predicate(path: Path, predicate, description: str,
                       timeout: int = 8) -> str:
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        if path.is_file():
            last = path.read_text(encoding="utf-8", errors="replace")
            if predicate(last):
                return last
        time.sleep(0.1)
    raise RuntimeError(
        f"no {description} observed after USB event injection\n"
        f"--- serial log ---\n{last[-12000:]}"
    )


def build_command(args: argparse.Namespace, qmp_socket: Path,
                  serial_log: Path, vars_copy: Path) -> list[str]:
    command = [
        args.qemu,
        "-machine", "q35",
        "-cpu", "qemu64",
        "-m", "512M",
        "-device", "qemu-xhci,id=xhci",
        "-drive", f"if=pflash,format=raw,readonly=on,file={args.firmware_code}",
        "-drive", f"if=pflash,format=raw,file={vars_copy}",
        "-display", "none",
        "-serial", f"file:{serial_log}",
        "-qmp", f"unix:{qmp_socket},server=on,wait=off",
        "-monitor", "none",
        "-no-reboot",
        "-no-shutdown",
    ]
    if args.topology == "hub":
        # QEMU routes USB devices through hierarchical port paths on the
        # controller's bus. A hub on root port 1 exposes downstream ports
        # 1.1, 1.2, etc.; usb-hub does not create a "<id>.0" bus.
        command += [
            "-device", "usb-hub,id=nexus-usb-hub,bus=xhci.0,port=1",
            "-device", "usb-kbd,id=nexus-usb-keyboard,bus=xhci.0,port=1.1",
            "-device", "usb-mouse,id=nexus-usb-mouse,bus=xhci.0,port=1.2",
        ]
    else:
        command += [
            "-device", "usb-kbd,id=nexus-usb-keyboard,bus=xhci.0",
            "-device", "usb-mouse,id=nexus-usb-mouse,bus=xhci.0",
        ]
    if args.mode == "installer":
        command += ["-cdrom", str(args.iso), "-boot", "d"]
    else:
        command += [
            "-drive", f"file={args.disk},if=none,id=installed,format=raw",
        ]
        if args.disk_interface == "nvme":
            command += [
                "-device", "nvme,drive=installed,serial=NEXUSINPUT0001,bootindex=1",
            ]
        else:
            command += [
                "-device", "usb-storage,bus=xhci.0,drive=installed,bootindex=1",
            ]
        command += ["-boot", "order=c"]
    return command


def usb_mouse_click_dispatched(log: str) -> bool:
    return any(
        line.startswith("INPUT: mouse dx=")
        and "buttons=0x0000000000000001" in line
        for line in log.splitlines()
    )


def exercise_usb_inputs(qmp: QMP, serial_log: Path, topology: str) -> None:
    mice = qmp.command("query-mice")
    if not isinstance(mice, list):
        raise RuntimeError(f"QEMU query-mice returned unexpected data: {mice!r}")
    usb_mice = [
        item for item in mice
        if isinstance(item, dict)
        and "ps/2" not in str(item.get("name", "")).lower()
        and (
            "usb" in str(item.get("name", "")).lower()
            or "hid" in str(item.get("name", "")).lower()
        )
    ]
    if not usb_mice:
        raise RuntimeError(
            "QEMU did not register a USB mouse in its input device list: "
            + json.dumps(mice, sort_keys=True)
        )
    selected = usb_mice[0]
    index = selected.get("index")
    qmp.hmp(f"mouse_set {int(index)}")
    selected_after = qmp.command("query-mice")
    current = [
        item for item in selected_after
        if isinstance(item, dict) and item.get("current") is True
    ]
    if not any(
        "ps/2" not in str(item.get("name", "")).lower()
        and (
            "usb" in str(item.get("name", "")).lower()
            or "hid" in str(item.get("name", "")).lower()
        )
        for item in current
    ):
        raise RuntimeError(
            "QEMU did not make the USB mouse the active pointer: "
            + json.dumps(selected_after, sort_keys=True)
        )

    # Send an actual USB keyboard usage-4 ("A") press and release.
    qmp.input([{
        "type": "key",
        "data": {
            "down": True,
            "key": {"type": "qcode", "data": "a"},
        },
    }])
    time.sleep(0.12)
    qmp.input([{
        "type": "key",
        "data": {
            "down": False,
            "key": {"type": "qcode", "data": "a"},
        },
    }])
    wait_for_predicate(
        serial_log,
        lambda log: (
            "INPUT: USB HID keyboard scancode=0x000000000000001E" in log
            and "INPUT: USB HID keyboard scancode=0x000000000000009E" in log
            and "INPUT: keyboard scancode=0x000000000000001E" in log
            and "INPUT: keyboard scancode=0x000000000000009E" in log
        ),
        "USB keyboard make/break in the desktop event dispatcher",
    )

    # Move and click through the selected USB mouse, then verify both the
    # USB-specific decoded report and the common installer/desktop event loop.
    qmp.hmp("mouse_move 48 24")
    time.sleep(0.15)
    qmp.hmp("mouse_button 1")
    time.sleep(0.12)
    qmp.hmp("mouse_button 0")
    wait_for_predicate(
        serial_log,
        lambda log: (
            "INPUT: USB HID mouse first report" in log
            and usb_mouse_click_dispatched(log)
        ),
        "USB mouse motion and left-button events in the desktop event dispatcher",
    )

    # QMP models a wheel notch as a wheel-up button press and release.
    qmp.input([
        {"type": "btn", "data": {"button": "wheel-up", "down": True}},
        {"type": "btn", "data": {"button": "wheel-up", "down": False}},
    ])
    wait_for_predicate(
        serial_log,
        lambda log: any(
            line.startswith("INPUT: mouse ")
            and "wheel=0x0000000000000001" in line
            for line in log.splitlines()
        ),
        "USB mouse wheel-up event in the desktop event dispatcher",
    )
    print(f"USB HID mouse selected: {selected.get('name')} (index {index})")
    print("USB keyboard A make/break reached Nexus input dispatcher")
    print("USB mouse motion/click/wheel reached Nexus input dispatcher")

    # Exercise USB hot-unplug/replug, not just boot-time enumeration. The
    # kernel must release device state and discover the reinserted peripherals.
    qmp.command("device_del", {"id": "nexus-usb-keyboard"})
    wait_for_predicate(
        serial_log,
        lambda log: (
            "USB: HID keyboard disconnected from xHCI port" in log
            or "USB: HID keyboard disconnected from hub slot=" in log
            or "xHCI: USB keyboard transfer failed; code=" in log
        ),
        "USB keyboard disconnect after QMP hot-unplug",
    )
    qmp.command("device_del", {"id": "nexus-usb-mouse"})
    wait_for_predicate(
        serial_log,
        lambda log: (
            "USB: HID mouse disconnected from xHCI port" in log
            or "USB: HID mouse disconnected from hub slot=" in log
            or "xHCI: USB mouse transfer failed; code=" in log
        ),
        "USB mouse disconnect after QMP hot-unplug",
    )

    qmp.command("device_add", {
        "driver": "usb-kbd", "id": "nexus-usb-keyboard-replugged",
        "bus": "xhci.0",
        **({"port": "1.1"} if topology == "hub" else {}),
    })
    qmp.command("device_add", {
        "driver": "usb-mouse", "id": "nexus-usb-mouse-replugged",
        "bus": "xhci.0",
        **({"port": "1.2"} if topology == "hub" else {}),
    })
    wait_for_predicate(
        serial_log,
        lambda log: (
            log.count("NEXUS USB HID KEYBOARD READY") >= 2
            and log.count("NEXUS USB HID MOUSE READY") >= 2
        ),
        "USB keyboard and mouse re-enumeration after hot-replug",
        timeout=20,
    )
    print("USB keyboard and mouse hot-unplug/replug recovered")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--mode", choices=("installer", "desktop"), required=True)
    parser.add_argument("--topology", choices=("direct", "hub"), default="direct")
    parser.add_argument("--iso", type=Path)
    parser.add_argument("--disk", type=Path)
    parser.add_argument("--disk-interface", choices=("usb-storage", "nvme"),
                        default="usb-storage")
    parser.add_argument("--firmware-code", type=Path, required=True)
    parser.add_argument("--firmware-vars", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()

    if args.mode == "installer" and (args.iso is None or not args.iso.is_file()):
        parser.error("--iso is required for installer mode")
    if args.mode == "desktop" and (args.disk is None or not args.disk.is_file()):
        parser.error("--disk is required for desktop mode")
    if not args.firmware_code.is_file() or not args.firmware_vars.is_file():
        parser.error("OVMF firmware code/variables files must exist")

    with tempfile.TemporaryDirectory(prefix="nexus-usb-input-") as temporary:
        temp = Path(temporary)
        qmp_socket = temp / "qmp.sock"
        serial_log = temp / "serial.log"
        qemu_stderr = temp / "qemu.stderr"
        vars_copy = temp / "OVMF_VARS.fd"
        shutil.copyfile(args.firmware_vars, vars_copy)
        command = build_command(args, qmp_socket, serial_log, vars_copy)
        with qemu_stderr.open("wb") as stderr_stream:
            process = subprocess.Popen(
                command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=stderr_stream,
            )
        qmp = None
        try:
            if args.topology == "hub":
                required = (
                    "NEXUS K0 BOOT COMPLETE",
                    "NEXUS USB HUB READY",
                )
            else:
                required = (
                    "NEXUS K0 BOOT COMPLETE",
                    "NEXUS USB HID KEYBOARD READY",
                    "NEXUS USB HID MOUSE READY",
                    "INPUT: USB HID boot keyboard available",
                    "INPUT: USB HID boot mouse available",
                )
            if args.mode == "installer":
                required += (
                    "NEXUS BOOT MODE: DEDICATED INSTALLER",
                    "NEXUS INSTALLER SESSION READY",
                )
            else:
                required += ("NEXUS BOOT MODE: DESKTOP",)

            # Wait for both boot and the QMP socket; fail immediately if QEMU dies.
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    diagnostic = qemu_stderr.read_text(
                        encoding="utf-8", errors="replace"
                    ) if qemu_stderr.is_file() else ""
                    raise RuntimeError(
                        f"QEMU exited early with status {process.returncode}"
                        + (f"\\n--- QEMU stderr ---\\n{diagnostic[-8000:]}"
                           if diagnostic else "\\nQEMU produced no stderr output.")
                    )
                if qmp_socket.exists():
                    try:
                        qmp = QMP(qmp_socket)
                        break
                    except (OSError, RuntimeError, socket.timeout):
                        if qmp is not None:
                            qmp.close()
                            qmp = None
                time.sleep(0.1)
            if qmp is None:
                raise RuntimeError("QEMU QMP socket did not become available")
            wait_for_log(serial_log, required, args.timeout)
            if args.topology == "hub":
                wait_for_predicate(
                    serial_log,
                    lambda log: (
                        "NEXUS USB HID KEYBOARD READY" in log
                        and "NEXUS USB HID MOUSE READY" in log
                    ),
                    "keyboard and mouse enumeration downstream of USB hub",
                    timeout=args.timeout,
                )
            exercise_usb_inputs(qmp, serial_log, args.topology)
            final_log = serial_log.read_text(encoding="utf-8", errors="replace")
            print(f"NEXUS {args.mode.upper()} USB INPUT INTEGRATION TEST PASSED (topology={args.topology})")
            print(final_log[-2500:])
        finally:
            if qmp is not None:
                qmp.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)


if __name__ == "__main__":
    main()
