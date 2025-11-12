#!/usr/bin/env python3
"""
TCP Logging Server for Hydromatic ESP32 Device

Receives JSON-line formatted log entries and heartbeats from ESP32 device,
sends acknowledgments, and logs all received data to file.

Protocol:
- Device sends: {"boot_seq":N,"uptime_ms":M,"seq":S,"level":"info","msg":"...","system":{...}}\n
- Device sends: {"boot_seq":N,"uptime_ms":M,"ts":null|"...","type":"heartbeat","system":{...}}\n
- Server responds: {"ack":1}\n

Features:
- Listens on port 5000 (configurable)
- Single device connection at a time
- Logs all received entries to JSON-line file
- Per-message acknowledgments
- Graceful error handling (malformed JSON → no ack → device retries)
- Auto-reconnect support
"""

import socket
import json
import argparse
import logging
import sys
from datetime import datetime, timezone
from typing import Optional, Dict, Any


# Configuration
DEFAULT_PORT = 5000
DEFAULT_HOST = '0.0.0.0'  # Listen on all interfaces
DEFAULT_LOG_FILE = 'tcp_server.log'
SOCKET_TIMEOUT = 30.0  # 30 second timeout for socket operations
BUFFER_SIZE = 4096
MAX_LINE_LENGTH = 16384  # Max 16KB per line


class TCPLogServer:
    """TCP server that receives log entries from ESP32 device"""

    def __init__(self, host: str, port: int, log_file: str):
        self.host = host
        self.port = port
        self.log_file = log_file
        self.server_socket: Optional[socket.socket] = None
        self.client_socket: Optional[socket.socket] = None
        self.client_address: Optional[tuple] = None
        self.running = False
        self.log_file_handle = None

        # Setup logging to stderr
        logging.basicConfig(
            level=logging.INFO,
            format='[%(asctime)s] [%(levelname)s] %(message)s',
            stream=sys.stderr
        )
        self.logger = logging.getLogger(__name__)

    def start(self):
        """Start the TCP server"""
        self.running = True

        # Open log file for appending
        try:
            self.log_file_handle = open(self.log_file, 'a', encoding='utf-8')
            self.logger.info(f"Logging to file: {self.log_file}")
        except Exception as e:
            self.logger.error(f"Failed to open log file: {e}")
            return

        # Create server socket
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.bind((self.host, self.port))
            self.server_socket.listen(1)  # Only accept 1 connection at a time
            self.logger.info(f"Server listening on {self.host}:{self.port}")
        except Exception as e:
            self.logger.error(f"Failed to start server: {e}")
            self.cleanup()
            return

        # Main server loop
        try:
            while self.running:
                self.logger.info("Waiting for device connection...")
                try:
                    self.client_socket, self.client_address = self.server_socket.accept()
                    self.client_socket.settimeout(SOCKET_TIMEOUT)
                    self.logger.info(f"Device connected from {self.client_address}")

                    # Handle this client connection
                    self.handle_client()

                except socket.timeout:
                    self.logger.warning("Accept timeout, retrying...")
                    continue
                except Exception as e:
                    self.logger.error(f"Error accepting connection: {e}")
                    continue
        except KeyboardInterrupt:
            self.logger.info("Server interrupted by user")
        finally:
            self.cleanup()

    def handle_client(self):
        """Handle a connected client (device)"""
        buffer = ""

        try:
            while self.running:
                # Receive data from client
                try:
                    data = self.client_socket.recv(BUFFER_SIZE)
                    if not data:
                        # Connection closed by client
                        self.logger.info("Device disconnected")
                        break

                    # Decode and add to buffer
                    try:
                        buffer += data.decode('utf-8')
                    except UnicodeDecodeError as e:
                        self.logger.error(f"Unicode decode error: {e}, raw data: {data.hex()}")
                        # Discard malformed data
                        buffer = ""
                        continue

                    # Process complete lines (JSON-line format)
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)

                        # Check line length
                        if len(line) > MAX_LINE_LENGTH:
                            self.logger.error(f"Line too long ({len(line)} bytes), discarding")
                            continue

                        # Process this line
                        if line.strip():  # Skip empty lines
                            self.process_line(line.strip())

                    # Check buffer size to prevent memory exhaustion
                    if len(buffer) > MAX_LINE_LENGTH:
                        self.logger.error(f"Buffer overflow ({len(buffer)} bytes), discarding")
                        buffer = ""

                except socket.timeout:
                    self.logger.warning("Socket timeout, checking connection...")
                    # Send a probe or just continue
                    continue
                except socket.error as e:
                    self.logger.error(f"Socket error: {e}")
                    break
                except Exception as e:
                    self.logger.error(f"Unexpected error in receive loop: {e}")
                    break

        finally:
            # Close client connection
            if self.client_socket:
                try:
                    self.client_socket.close()
                except:
                    pass
                self.client_socket = None
                self.client_address = None

    def process_line(self, line: str):
        """
        Process a received JSON line from device

        Expected formats:
        - Log entry: {"boot_seq":N,"uptime_ms":M,"seq":S,"ts":null|"...","level":"...","msg":"...","system":{...}}
        - Heartbeat: {"boot_seq":N,"uptime_ms":M,"ts":null|"...","type":"heartbeat","system":{...}}
        """
        try:
            # Parse JSON
            entry = json.loads(line)

            # Validate basic structure (must have boot_seq and uptime_ms at minimum)
            if 'boot_seq' not in entry or 'uptime_ms' not in entry:
                self.logger.error(f"Invalid entry structure (missing boot_seq or uptime_ms): {line}")
                # No ack → device will retry
                return

            # Log to file with server receive timestamp
            log_entry = {
                "received_at": datetime.now(timezone.utc).isoformat(),
                "entry": entry
            }

            # Write to log file
            try:
                json_line = json.dumps(log_entry, separators=(',', ':'))
                self.log_file_handle.write(json_line + '\n')
                self.log_file_handle.flush()  # Flush after each write
            except Exception as e:
                self.logger.error(f"Failed to write to log file: {e}")

            # Determine entry type for logging
            entry_type = entry.get('type', 'log')
            if entry_type == 'heartbeat':
                self.logger.info(f"HEARTBEAT: boot_seq={entry['boot_seq']}, uptime_ms={entry['uptime_ms']}")
            else:
                level = entry.get('level', 'unknown')
                seq = entry.get('seq', '?')
                msg = entry.get('msg', '')[:80]  # Truncate for stderr display
                self.logger.info(f"LOG: boot_seq={entry['boot_seq']}, seq={seq}, level={level}, msg={msg}")

            # Send acknowledgment
            self.send_ack()

        except json.JSONDecodeError as e:
            self.logger.error(f"JSON decode error: {e}, line: {line[:200]}")
            # No ack → device will retry
        except Exception as e:
            self.logger.error(f"Error processing line: {e}")
            # No ack → device will retry

    def send_ack(self):
        """Send acknowledgment to device"""
        ack = {"ack": 1}
        try:
            ack_json = json.dumps(ack) + '\n'
            self.client_socket.sendall(ack_json.encode('utf-8'))
        except Exception as e:
            self.logger.error(f"Failed to send ack: {e}")
            # Connection likely broken, will be detected in next receive

    def cleanup(self):
        """Clean up resources"""
        self.logger.info("Cleaning up server resources...")

        if self.client_socket:
            try:
                self.client_socket.close()
            except:
                pass

        if self.server_socket:
            try:
                self.server_socket.close()
            except:
                pass

        if self.log_file_handle:
            try:
                self.log_file_handle.close()
            except:
                pass

        self.logger.info("Server stopped")


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='TCP Logging Server for Hydromatic ESP32 Device',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Start server with defaults (port 5000, log to tcp_server.log)
  %(prog)s

  # Specify custom log file
  %(prog)s --log-file /var/log/hydromatic.jsonl

  # Specify custom port
  %(prog)s --port 5001
        """
    )

    parser.add_argument(
        '--host',
        default=DEFAULT_HOST,
        help=f'Host to bind to (default: {DEFAULT_HOST})'
    )

    parser.add_argument(
        '--port',
        type=int,
        default=DEFAULT_PORT,
        help=f'Port to listen on (default: {DEFAULT_PORT})'
    )

    parser.add_argument(
        '--log-file',
        default=DEFAULT_LOG_FILE,
        help=f'Output log file (default: {DEFAULT_LOG_FILE})'
    )

    args = parser.parse_args()

    # Create and start server
    server = TCPLogServer(args.host, args.port, args.log_file)
    server.start()


if __name__ == '__main__':
    main()
