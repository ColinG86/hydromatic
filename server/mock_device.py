#!/usr/bin/env python3
"""
Mock ESP32 Device for Testing TCP Log Server

Simulates an ESP32 device sending log entries and heartbeats to the server.
Implements the full protocol including acknowledgment waiting and retries.

Features:
- Sends sample log entries with boot_seq, seq, system stats
- Sends heartbeats every 1 second of idle time
- Waits for acknowledgments ({"ack":1})
- Retries on timeout or missing ack
- Simulates both synced (with ts) and unsynced (ts:null) scenarios
"""

import socket
import json
import time
import argparse
import sys
from datetime import datetime, timezone
from typing import Optional


class MockDevice:
    """Mock ESP32 device that sends logs and heartbeats"""

    def __init__(self, host: str, port: int, boot_seq: int = 1):
        self.host = host
        self.port = port
        self.boot_seq = boot_seq
        self.seq = 0
        self.uptime_ms = 0
        self.start_time = time.time()
        self.socket: Optional[socket.socket] = None
        self.connected = False
        self.has_ntp_sync = False  # Simulate no NTP sync initially

    def connect(self) -> bool:
        """Connect to server"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(5.0)
            self.socket.connect((self.host, self.port))
            self.connected = True
            print(f"[DEVICE] Connected to {self.host}:{self.port}", file=sys.stderr)
            return True
        except Exception as e:
            print(f"[DEVICE] Connection failed: {e}", file=sys.stderr)
            self.connected = False
            return False

    def disconnect(self):
        """Disconnect from server"""
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
            self.connected = False
            print("[DEVICE] Disconnected", file=sys.stderr)

    def send_entry(self, entry: dict, retry_count: int = 3) -> bool:
        """
        Send an entry and wait for ack
        Returns True if ack received, False otherwise
        """
        if not self.connected:
            if not self.connect():
                return False

        # Send entry
        try:
            json_line = json.dumps(entry) + '\n'
            self.socket.sendall(json_line.encode('utf-8'))
            print(f"[DEVICE] Sent: {json.dumps(entry)[:100]}...", file=sys.stderr)

            # Wait for ack
            self.socket.settimeout(2.0)
            response = self.socket.recv(1024)
            if response:
                try:
                    ack = json.loads(response.decode('utf-8').strip())
                    if ack.get('ack') == 1:
                        print("[DEVICE] Ack received", file=sys.stderr)
                        return True
                    else:
                        print(f"[DEVICE] Invalid ack: {ack}", file=sys.stderr)
                except json.JSONDecodeError:
                    print(f"[DEVICE] Invalid ack JSON: {response}", file=sys.stderr)

        except socket.timeout:
            print("[DEVICE] Ack timeout", file=sys.stderr)
        except Exception as e:
            print(f"[DEVICE] Send error: {e}", file=sys.stderr)
            self.disconnect()

        # Retry logic
        if retry_count > 0:
            print(f"[DEVICE] Retrying... ({retry_count} attempts left)", file=sys.stderr)
            time.sleep(1)
            return self.send_entry(entry, retry_count - 1)

        return False

    def get_system_stats(self) -> dict:
        """Generate mock system stats"""
        import random
        return {
            "heap_free": 200000 + random.randint(-10000, 10000),
            "heap_used": 50000 + random.randint(-5000, 5000),
            "uptime_ms": self.uptime_ms,
            "free_psram": 4194304,
            "task_count": 3,
            "spiffs_free": 900000 + random.randint(-50000, 50000),
            "spiffs_used": 100000 + random.randint(-10000, 10000)
        }

    def update_uptime(self):
        """Update simulated uptime"""
        elapsed = time.time() - self.start_time
        self.uptime_ms = int(elapsed * 1000)

    def send_log(self, level: str, msg: str) -> bool:
        """Send a log entry"""
        self.update_uptime()

        entry = {
            "boot_seq": self.boot_seq,
            "uptime_ms": self.uptime_ms,
            "seq": self.seq,
            "level": level,
            "msg": msg,
            "system": self.get_system_stats()
        }

        # Add timestamp if NTP synced
        if self.has_ntp_sync:
            entry["ts"] = datetime.now(timezone.utc).isoformat()
        else:
            entry["ts"] = None

        success = self.send_entry(entry)
        if success:
            self.seq += 1

        return success

    def send_heartbeat(self) -> bool:
        """Send a heartbeat"""
        self.update_uptime()

        heartbeat = {
            "boot_seq": self.boot_seq,
            "uptime_ms": self.uptime_ms,
            "type": "heartbeat",
            "system": self.get_system_stats()
        }

        # Add timestamp if NTP synced
        if self.has_ntp_sync:
            heartbeat["ts"] = datetime.now(timezone.utc).isoformat()
        else:
            heartbeat["ts"] = None

        return self.send_entry(heartbeat)

    def run_test_sequence(self):
        """Run a test sequence of logs and heartbeats"""
        print("[DEVICE] Starting test sequence...", file=sys.stderr)

        # Send some initial logs (no NTP sync)
        print("\n=== Phase 1: Logs without NTP sync (ts:null) ===", file=sys.stderr)
        self.send_log("info", "Device booting...")
        time.sleep(0.5)
        self.send_log("info", "WiFi connecting...")
        time.sleep(0.5)
        self.send_log("info", "WiFi connected to WAP")
        time.sleep(0.5)

        # Simulate NTP sync
        print("\n=== Phase 2: NTP sync acquired ===", file=sys.stderr)
        self.has_ntp_sync = True
        self.send_log("info", "NTP sync successful")
        time.sleep(0.5)

        # Send logs with timestamps
        print("\n=== Phase 3: Logs with timestamps ===", file=sys.stderr)
        self.send_log("info", "Logger initialized")
        time.sleep(0.5)
        self.send_log("debug", "System stats: heap=200KB, SPIFFS=900KB free")
        time.sleep(0.5)
        self.send_log("warning", "Temperature sensor not responding")
        time.sleep(0.5)
        self.send_log("error", "Failed to read sensor (retry 1/3)")
        time.sleep(0.5)

        # Send heartbeats
        print("\n=== Phase 4: Heartbeats (idle periods) ===", file=sys.stderr)
        for i in range(3):
            self.send_heartbeat()
            time.sleep(1.0)

        # More logs
        print("\n=== Phase 5: More logs ===", file=sys.stderr)
        self.send_log("info", "Sensor recovered, reading: 23.5°C")
        time.sleep(0.5)
        self.send_log("info", "All systems nominal")

        print("\n[DEVICE] Test sequence complete!", file=sys.stderr)
        self.disconnect()


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Mock ESP32 Device for Testing TCP Log Server'
    )

    parser.add_argument(
        '--host',
        default='localhost',
        help='Server host (default: localhost)'
    )

    parser.add_argument(
        '--port',
        type=int,
        default=5000,
        help='Server port (default: 5000)'
    )

    parser.add_argument(
        '--boot-seq',
        type=int,
        default=1,
        help='Boot sequence number (default: 1)'
    )

    args = parser.parse_args()

    # Create mock device and run test
    device = MockDevice(args.host, args.port, args.boot_seq)
    device.run_test_sequence()


if __name__ == '__main__':
    main()
