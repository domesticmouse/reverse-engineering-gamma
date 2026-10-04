#!/usr/bin/env python3
"""
gamma_usb.py - USB Communication Utility for the this.is.NOISE Gamma Mini Synth
Daisy Seed 2 DFM (STM32H750) USB CDC Virtual COM Port.

Supports automated test mode, direct command dispatch, live monitoring,
and interactive terminal mode. Runs with Python standard library (no external
dependencies required on macOS/Linux), and automatically uses pyserial if installed.
"""

import argparse
import glob
import os
import sys
import time
import select

try:
    import termios
    import tty
    HAVE_TERMIOS = True
except ImportError:
    HAVE_TERMIOS = False

try:
    import serial
    HAVE_SERIAL = True
except ImportError:
    HAVE_SERIAL = False


def find_gamma_port():
    """Auto-detect the Gamma synth USB CDC serial device."""
    candidates = []
    if sys.platform == "darwin":
        candidates = sorted(glob.glob("/dev/cu.usbmodem*"))
    elif sys.platform.startswith("linux"):
        candidates = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/serial/by-id/*Daisy*") + glob.glob("/dev/serial/by-id/*Electrosmith*"))
    elif sys.platform == "win32":
        if HAVE_SERIAL:
            import serial.tools.list_ports
            for p in serial.tools.list_ports.comports():
                if "Daisy" in p.description or (p.vid == 0x0483 and p.pid == 0x5740):
                    candidates.append(p.device)
    
    if candidates:
        return candidates[0]
    return None


class PosixSerialConnection:
    """Zero-dependency serial connection using standard POSIX termios/os."""
    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.fd = None

    def open(self):
        if not HAVE_TERMIOS:
            raise RuntimeError("POSIX termios not available on this platform.")
        self.fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        tty.setraw(self.fd)
        
        # Set baudrate
        baud_const = getattr(termios, f"B{self.baudrate}", termios.B115200)
        attrs[4] = baud_const  # ispeed
        attrs[5] = baud_const  # ospeed
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)

    def write(self, data: bytes):
        if self.fd is None:
            raise RuntimeError("Port not open")
        return os.write(self.fd, data)

    def read(self, max_bytes=1024, timeout=0.1) -> bytes:
        if self.fd is None:
            raise RuntimeError("Port not open")
        r, _, _ = select.select([self.fd], [], [], timeout)
        if r:
            try:
                return os.read(self.fd, max_bytes)
            except BlockingIOError:
                return b""
        return b""

    def close(self):
        if self.fd is not None:
            try:
                os.close(self.fd)
            except Exception:
                pass
            self.fd = None


class PySerialConnection:
    """Wrapper around pyserial when available."""
    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.ser = None

    def open(self):
        self.ser = serial.Serial(self.port, self.baudrate, timeout=0.1)

    def write(self, data: bytes):
        if self.ser is None:
            raise RuntimeError("Port not open")
        return self.ser.write(data)

    def read(self, max_bytes=1024, timeout=0.1) -> bytes:
        if self.ser is None:
            raise RuntimeError("Port not open")
        self.ser.timeout = timeout
        return self.ser.read(max_bytes)

    def close(self):
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None


def open_connection(port, baudrate=115200):
    if HAVE_SERIAL:
        conn = PySerialConnection(port, baudrate)
    elif HAVE_TERMIOS:
        conn = PosixSerialConnection(port, baudrate)
    else:
        raise RuntimeError("No serial driver available (requires pyserial on Windows or POSIX termios).")
    conn.open()
    return conn


def drain(conn, duration=0.3):
    t_end = time.time() + duration
    data = b""
    while time.time() < t_end:
        chunk = conn.read(1024, timeout=0.05)
        if chunk:
            data += chunk
    return data


def run_test(conn):
    print("=" * 60)
    print("  Gamma Mini Synth USB CDC Self-Test")
    print("=" * 60)
    print(f"Device connected on: {conn.port}")
    
    # Drain any buffered logs
    initial = drain(conn, duration=0.4)
    if initial:
        print(f"[Initial Output]: {initial.decode(errors='replace').strip()}")

    # Test 1: Send 'h' (Help / Menu)
    print("\n[Test 1] Sending 'h' (Help Menu)...")
    conn.write(b'h')
    
    t_end = time.time() + 1.2
    resp = b""
    while time.time() < t_end:
        chunk = conn.read(1024, timeout=0.05)
        if chunk:
            resp += chunk
            if b"Gamma Phase" in resp or b"Audio Commands" in resp:
                break
    
    decoded = resp.decode(errors='replace').strip()
    if decoded:
        print(f"Response received ({len(resp)} bytes):\n{decoded}")
    else:
        print("No response received within timeout.")

    test1_pass = (b"Gamma" in resp or b"Commands" in resp or len(resp) > 0)
    print(f"Test 1 Result: {'PASS' if test1_pass else 'FAIL'}")

    # Test 2: Send 'w' (Cycle Waveform)
    print("\n[Test 2] Sending 'w' (Cycle Waveform)...")
    conn.write(b'w')
    
    t_end = time.time() + 1.2
    w_resp = b""
    while time.time() < t_end:
        chunk = conn.read(1024, timeout=0.05)
        if chunk:
            w_resp += chunk
            if b"[WAVE]" in w_resp:
                break

    w_decoded = w_resp.decode(errors='replace').strip()
    if w_decoded:
        print(f"Response received:\n{w_decoded}")
    else:
        print("No response received.")

    test2_pass = (b"[WAVE]" in w_resp or len(w_resp) > 0)
    print(f"Test 2 Result: {'PASS' if test2_pass else 'FAIL'}")

    # Test 3: Send 's' (Speaker Toggle) twice to leave state unchanged
    print("\n[Test 3] Sending 's' (Toggle Speaker Mute x2)...")
    conn.write(b's')
    time.sleep(0.1)
    conn.write(b's')
    s_resp = drain(conn, duration=0.6)
    s_decoded = s_resp.decode(errors='replace').strip()
    if s_decoded:
        print(f"Response received:\n{s_decoded}")
    test3_pass = (b"[SPK]" in s_resp or len(s_resp) > 0)
    print(f"Test 3 Result: {'PASS' if test3_pass else 'FAIL'}")

    print("\n" + "=" * 60)
    all_pass = test1_pass and test2_pass and test3_pass
    print(f"Overall USB Connectivity: {'PASSED (HEALTHY)' if all_pass else 'FAILED / UNRESPONSIVE'}")
    print("=" * 60)
    return 0 if all_pass else 1


def run_monitor(conn):
    print(f"Listening on {conn.port} (Ctrl+C to stop)...")
    try:
        while True:
            chunk = conn.read(1024, timeout=0.2)
            if chunk:
                sys.stdout.write(chunk.decode(errors='replace'))
                sys.stdout.flush()
    except KeyboardInterrupt:
        print("\nMonitor stopped.")


def run_command(conn, cmd: str):
    drain(conn, duration=0.2)
    print(f"Sending command: '{cmd}'...")
    conn.write(cmd.encode('ascii'))
    resp = drain(conn, duration=0.8)
    print("Response:\n" + resp.decode(errors='replace').strip())


def run_interactive(conn):
    if not HAVE_TERMIOS:
        print("Interactive raw mode requires POSIX termios. Use --monitor or --cmd instead.")
        return

    print(f"Entering interactive mode on {conn.port}. Press Ctrl+C or Ctrl+] to exit.")
    old_stdin_attr = termios.tcgetattr(sys.stdin.fileno())
    tty.setraw(sys.stdin.fileno())

    try:
        while True:
            rlist, _, _ = select.select([sys.stdin.fileno(), conn.fd if hasattr(conn, 'fd') else None], [], [], 0.05)
            if sys.stdin.fileno() in rlist:
                ch = os.read(sys.stdin.fileno(), 1)
                if ch in (b'\x03', b'\x1d'):  # Ctrl+C or Ctrl+]
                    break
                conn.write(ch)

            if hasattr(conn, 'fd') and conn.fd in rlist:
                out = os.read(conn.fd, 1024)
                if out:
                    # Echo to stdout translating bare newlines if needed
                    os.write(sys.stdout.fileno(), out)
    finally:
        termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, old_stdin_attr)
        print("\nExited interactive mode.")


def main():
    parser = argparse.ArgumentParser(description="Gamma Mini Synth USB CDC Host Tool")
    parser.add_argument("--port", "-p", help="Serial port device (default: auto-detect)", default=None)
    parser.add_argument("--baud", "-b", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--test", "-t", action="store_true", help="Run automated USB bidirectional test")
    parser.add_argument("--monitor", "-m", action="store_true", help="Monitor incoming serial logs")
    parser.add_argument("--cmd", "-c", help="Send a command character or string")
    parser.add_argument("--interactive", "-i", action="store_true", help="Interactive terminal mode")
    parser.add_argument("--bootloader", action="store_true", help="Reboot synth into Daisy DFU bootloader ('b')")

    args = parser.parse_args()

    port = args.port or find_gamma_port()
    if not port:
        print("ERROR: No Gamma synth USB device found. Is it plugged in and powered on?", file=sys.stderr)
        sys.exit(1)

    print(f"Opening port: {port}")
    try:
        conn = open_connection(port, args.baud)
    except Exception as e:
        print(f"ERROR: Failed to open {port}: {e}", file=sys.stderr)
        sys.exit(1)

    try:
        if args.bootloader:
            print("Sending bootloader reboot request ('b')...")
            conn.write(b'b')
            drain(conn, duration=0.3)
            print("Reboot requested. Device should now be in DFU bootloader mode.")
            return 0
        elif args.test:
            return run_test(conn)
        elif args.cmd:
            run_command(conn, args.cmd)
        elif args.interactive:
            run_interactive(conn)
        elif args.monitor:
            run_monitor(conn)
        else:
            # Default to test mode
            return run_test(conn)
    finally:
        conn.close()


if __name__ == '__main__':
    sys.exit(main() or 0)
