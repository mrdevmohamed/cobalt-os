"""Boot the real OS and exercise PS/2 input, shell edges, and GUI via QMP."""
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


def png_from_ppm(source, destination):
    # QEMU's P6 screenshot has no comments and stores eight-bit RGB channels.
    with source.open("rb") as f:
        assert f.readline() == b"P6\n"
        width, height = map(int, f.readline().split())
        assert f.readline() == b"255\n"
        pixels = f.read()
    assert len(pixels) == width * height * 3
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    destination.write_bytes(b"\x89PNG\r\n\x1a\n" +
                           chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
    return width, height, pixels


def main():
    log = BUILD / "smoke-debug.log"
    with tempfile.TemporaryDirectory(prefix="cobalt-") as temporary:
        endpoint = str(Path(temporary) / "qmp")
        process = subprocess.Popen([
            "qemu-system-i386", "-drive", f"file={BUILD / 'cobalt.img'},format=raw,if=floppy",
            "-snapshot", "-boot", "a", "-m", "32", "-vga", "std", "-display", "none",
            "-qmp", f"unix:{endpoint},server=on,wait=off", "-debugcon", f"file:{log}",
            "-no-reboot", "-rtc", "base=utc",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        connection = socket.socket(socket.AF_UNIX)
        connection.settimeout(10)
        try:
            deadline = time.monotonic() + 10
            while not Path(endpoint).exists():
                if process.poll() is not None:
                    raise RuntimeError(process.stderr.read().decode())
                if time.monotonic() > deadline:
                    raise TimeoutError("QMP did not start")
                time.sleep(0.02)
            connection.connect(endpoint)
            stream = connection.makefile("rwb", buffering=0)
            assert "QMP" in json.loads(stream.readline())
            def qmp(command, arguments=None):
                stream.write(json.dumps({"execute": command, "arguments": arguments or {}}).encode() + b"\n")
                while True:
                    reply = json.loads(stream.readline())
                    if "error" in reply:
                        raise RuntimeError(reply)
                    if "return" in reply:
                        return reply["return"]
            qmp("qmp_capabilities")
            def await_log(text):
                until = time.monotonic() + 10
                while time.monotonic() < until:
                    if log.exists() and text in log.read_text():
                        return
                    if process.poll() is not None:
                        raise RuntimeError(f"QEMU exited before {text!r}")
                    time.sleep(0.02)
                raise AssertionError(f"Missing {text!r}\n{log.read_text() if log.exists() else ''}")
            def key(code, modifiers=()):
                events = []
                for name in (*modifiers, code):
                    events.append({"type": "key", "data": {"down": True, "key": {"type": "qcode", "data": name}}})
                for name in reversed((*modifiers, code)):
                    events.append({"type": "key", "data": {"down": False, "key": {"type": "qcode", "data": name}}})
                qmp("input-send-event", {"events": events})
                time.sleep(0.025)
            def type_text(text):
                mapping = {" ": "spc", ".": "dot", "-": "minus", "_": "minus"}
                for character in text:
                    key(mapping.get(character, character.lower()),
                        ("shift",) if character.isupper() or character == "_" else ())
            def command(text):
                type_text(text); key("ret")
            def relative(dx, dy):
                # Split movement to avoid overflowing the three-byte PS/2 packet.
                while dx or dy:
                    x = max(-80, min(80, dx)); y = max(-80, min(80, dy))
                    qmp("input-send-event", {"events": [
                        {"type": "rel", "data": {"axis": "x", "value": x}},
                        {"type": "rel", "data": {"axis": "y", "value": y}}]})
                    dx -= x; dy -= y; time.sleep(0.04)
            def button(down):
                qmp("input-send-event", {"events": [
                    {"type": "btn", "data": {"down": down, "button": "left"}}]})
                time.sleep(0.08)
            def screenshot(name):
                ppm = BUILD / f"{name}.ppm"
                qmp("screendump", {"filename": str(ppm)})
                result = png_from_ppm(ppm, BUILD / f"{name}.png")
                ppm.unlink()
                return result

            await_log("COBALT READY")
            width, height, pixels = screenshot("desktop")
            assert (width, height) == (800, 600)
            assert len(set(pixels[i:i + 3] for i in range(0, len(pixels), 3))) > 5
            command("help"); await_log("Show this list")
            command("echo hello from Cobalt"); await_log("hello from Cobalt\ncobalt> ")
            type_text("echo editx"); key("backspace"); key("ret"); await_log("edit\ncobalt> ")
            key("up"); key("ret")
            time.sleep(0.1)
            assert log.read_text().count("edit\ncobalt> ") == 2
            type_text("draft"); key("up"); key("down"); key("c", ("ctrl",))
            await_log("draft^C")
            command("ls"); await_log("README.TXT  ")
            command("cat README.TXT"); await_log("No processes, writable disk filesystem")
            command("cat MISSING"); await_log("File not found")
            command("color invalid"); await_log("Usage: color white|green|amber")
            command("color green")
            command("mem"); await_log("No heap; no RAM capacity probe.")
            command("date"); await_log(" (RTC)")
            command("uptime"); await_log(" seconds since boot")
            command("unknown"); await_log("Unknown command: unknown")
            # A full input line must not wrap into the next row or underflow on erase.
            type_text("echo " + "a" * 85)
            for _ in range(79): key("backspace")
            command("echo boundary-ok"); await_log("boundary-ok\ncobalt> ")
            key("l", ("ctrl",)); command("about"); command("help")
            # The PS/2 cursor starts at (680, 500). Minimize and restore by taskbar.
            relative(-600, 80)
            button(True); button(False); await_log("GUI: terminal minimized")
            screenshot("minimized")
            button(True); button(False); await_log("GUI: terminal restored")
            # Drag the title bar to prove this is an interactive graphical window.
            relative(120, -490)
            button(True); relative(20, 10); button(False)
            screenshot("smoke")
            command("echo gui-and-terminal-ok"); await_log("gui-and-terminal-ok\ncobalt> ")
            command("shutdown")
            process.wait(timeout=10)
            assert process.returncode == 0
            print("PASS: BIOS boot, 800x600 GUI, shell, editing boundary, history, ROM files,")
            print("      RTC, uptime, mouse minimize/restore/drag, and QEMU shutdown.")
            print("Screenshots: build/desktop.png, build/minimized.png, build/smoke.png")
        finally:
            connection.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
            errors = process.stderr.read().decode()
            if errors:
                print(f"QEMU exit {process.returncode}: {errors}")
            process.stderr.close()


if __name__ == "__main__":
    main()
