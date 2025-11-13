#!/usr/bin/env python3
"""
Simple, reliable serial monitor for ESP32 debugging.
Captures output to both stdout and a log file.

Usage:
    ./temp/serial-monitor.py [duration_seconds] [output_file]

Examples:
    ./temp/serial-monitor.py              # Monitor for 10 seconds, save to /tmp/serial.log
    ./temp/serial-monitor.py 30           # Monitor for 30 seconds
    ./temp/serial-monitor.py 30 /tmp/out  # Monitor for 30 seconds, save to /tmp/out
"""

import serial
import time
import sys
import os

# Configuration
PORT = '/dev/ttyUSB0'
BAUD = 115200
TIMEOUT = 1

# Parse arguments
duration = int(sys.argv[1]) if len(sys.argv) > 1 else 10
output_file = sys.argv[2] if len(sys.argv) > 2 else '/tmp/serial.log'

print(f"[SERIAL] Opening {PORT} at {BAUD} baud...", file=sys.stderr)
print(f"[SERIAL] Will capture for {duration} seconds", file=sys.stderr)
print(f"[SERIAL] Saving to: {output_file}", file=sys.stderr)
print(f"[SERIAL] " + "=" * 60, file=sys.stderr)

try:
    # Open serial port
    ser = serial.Serial(PORT, BAUD, timeout=TIMEOUT)
    time.sleep(0.5)  # Let port stabilize

    # Clear any existing output file
    with open(output_file, 'w') as f:
        f.write("")

    # Monitor serial output
    start_time = time.time()
    bytes_received = 0

    while time.time() - start_time < duration:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            bytes_received += len(data)

            # Decode and display
            text = data.decode('utf-8', errors='replace')
            print(text, end='', flush=True)

            # Append to file
            with open(output_file, 'a') as f:
                f.write(text)

        time.sleep(0.05)  # Small delay to prevent CPU spinning

    ser.close()

    print(f"\n[SERIAL] " + "=" * 60, file=sys.stderr)
    print(f"[SERIAL] Captured {bytes_received} bytes in {duration} seconds", file=sys.stderr)
    print(f"[SERIAL] Output saved to: {output_file}", file=sys.stderr)

    # Show file contents if small
    if os.path.getsize(output_file) < 5000:
        print(f"[SERIAL] File contents:", file=sys.stderr)
        with open(output_file, 'r') as f:
            print(f.read(), file=sys.stderr)

    sys.exit(0)

except serial.SerialException as e:
    print(f"[SERIAL] ERROR: Cannot open serial port: {e}", file=sys.stderr)
    sys.exit(1)
except KeyboardInterrupt:
    print(f"\n[SERIAL] Interrupted by user", file=sys.stderr)
    sys.exit(0)
except Exception as e:
    print(f"[SERIAL] ERROR: {e}", file=sys.stderr)
    sys.exit(1)
