# Hydromatic TCP Logging Server

TCP server for receiving log entries and heartbeats from Hydromatic ESP32 devices.

## Overview

This server receives JSON-line formatted log entries and heartbeats from ESP32 devices, sends acknowledgments, and logs all received data to a file for analysis.

**Protocol:**
- Device sends: Log entries or heartbeats (JSON-line format)
- Server responds: `{"ack":1}\n` per message
- Server logs: Appends to JSON-line file with receive timestamp

## Features

- **Simple TCP server** listening on port 5000 (configurable)
- **Per-message acknowledgments** for reliable delivery
- **JSON-line logging** for easy parsing and analysis
- **Graceful error handling** (malformed JSON → no ack → device retries)
- **Auto-reconnect support** (server waits for device reconnection)
- **Verbose logging** preserves all device data + server timestamps

## Installation

No external dependencies required - uses Python 3.8+ stdlib only.

```bash
# Verify Python version
python3 --version  # Should be 3.8 or higher

# Make scripts executable
chmod +x tcp_log_server.py mock_device.py
```

## Usage

### Starting the Server

```bash
# Start with defaults (port 5000, log to tcp_server.log)
python3 tcp_log_server.py

# Specify custom log file
python3 tcp_log_server.py --log-file /path/to/output.jsonl

# Specify custom port and host
python3 tcp_log_server.py --host 0.0.0.0 --port 5001 --log-file server.log
```

**Command-line options:**
- `--host` - Host to bind to (default: `0.0.0.0` - all interfaces)
- `--port` - Port to listen on (default: `5000`)
- `--log-file` - Output log file path (default: `tcp_server.log`)

**Server output:**
- Logs operational messages to **stderr** (connection events, errors)
- Logs received entries to **file** (JSON-line format)

### Testing with Mock Device

Use the included mock device script to test the server:

```bash
# In terminal 1: Start server
python3 tcp_log_server.py

# In terminal 2: Run mock device
python3 mock_device.py

# Or with custom parameters
python3 mock_device.py --host localhost --port 5000 --boot-seq 3
```

The mock device simulates:
- Device boot sequence with incrementing log entries
- NTP sync transition (ts:null → ISO 8601 timestamps)
- Heartbeats every 1 second of idle time
- Retry logic on missing acknowledgments

## Log File Format

Server outputs JSON-line format (one JSON object per line):

```json
{"received_at":"2025-11-12T19:08:58.838190+00:00","entry":{DEVICE_ENTRY}}
```

**Fields:**
- `received_at` - Server timestamp (ISO 8601, UTC) when entry was received
- `entry` - Complete device entry (log or heartbeat) as sent by device

### Log Entry Format (from device)

```json
{
  "boot_seq": 3,
  "uptime_ms": 45000,
  "seq": 42,
  "ts": "2025-11-12T19:08:58.837902+00:00",
  "level": "info",
  "msg": "WiFi connected",
  "system": {
    "heap_free": 200000,
    "heap_used": 50000,
    "uptime_ms": 45000,
    "free_psram": 4194304,
    "task_count": 3,
    "spiffs_free": 900000,
    "spiffs_used": 100000
  }
}
```

**Fields:**
- `boot_seq` - Boot sequence number (increments on each device boot)
- `uptime_ms` - Device uptime in milliseconds since boot
- `seq` - Entry sequence number (per-boot, increments with each log)
- `ts` - Timestamp (ISO 8601 if NTP synced, `null` if not synced)
- `level` - Log level (`debug`, `info`, `warning`, `error`)
- `msg` - Log message
- `system` - System statistics snapshot

### Heartbeat Format (from device)

```json
{
  "boot_seq": 3,
  "uptime_ms": 46000,
  "ts": "2025-11-12T19:08:59.837902+00:00",
  "type": "heartbeat",
  "system": {
    "heap_free": 200000,
    "heap_used": 50000,
    "uptime_ms": 46000,
    "free_psram": 4194304,
    "task_count": 3,
    "spiffs_free": 900000,
    "spiffs_used": 100000
  }
}
```

**Heartbeats:**
- Sent every 1 second of idle time (no log entries to send)
- Validate bidirectional connection is working
- Include same system stats as log entries
- Distinguished by `"type":"heartbeat"` field

## Analyzing Logs

The JSON-line format is designed for easy analysis:

### Using jq (Pretty-print)

```bash
# Pretty-print all entries
cat tcp_server.log | jq

# Show only log messages (not heartbeats)
cat tcp_server.log | jq 'select(.entry.type != "heartbeat")'

# Show only heartbeats
cat tcp_server.log | jq 'select(.entry.type == "heartbeat")'

# Filter by boot sequence
cat tcp_server.log | jq 'select(.entry.boot_seq == 3)'

# Show only error-level logs
cat tcp_server.log | jq 'select(.entry.level == "error")'

# Extract just messages
cat tcp_server.log | jq -r '.entry.msg'

# Show entries with null timestamps (no NTP sync)
cat tcp_server.log | jq 'select(.entry.ts == null)'
```

### Using grep

```bash
# Find all errors
grep '"level":"error"' tcp_server.log

# Find entries from specific boot
grep '"boot_seq":3' tcp_server.log

# Find heartbeats
grep '"type":"heartbeat"' tcp_server.log

# Count total entries
wc -l tcp_server.log
```

### Using Python

```python
import json

# Read and parse all entries
with open('tcp_server.log', 'r') as f:
    entries = [json.loads(line) for line in f]

# Analyze
print(f"Total entries: {len(entries)}")
logs = [e for e in entries if e['entry'].get('type') != 'heartbeat']
heartbeats = [e for e in entries if e['entry'].get('type') == 'heartbeat']
print(f"Logs: {len(logs)}, Heartbeats: {len(heartbeats)}")

# Get unique boot sequences
boot_seqs = set(e['entry']['boot_seq'] for e in entries)
print(f"Unique boots: {boot_seqs}")
```

## Protocol Details

### Communication Flow

1. **Server starts** and listens on configured port
2. **Device connects** via TCP
3. **Device sends** JSON-line entry (log or heartbeat)
4. **Server receives**, parses, validates, logs to file
5. **Server responds** with `{"ack":1}\n`
6. **Device receives ack**, continues with next entry
7. If **no ack** received: Device retries with exponential backoff
8. On **disconnect**: Server waits for reconnection

### Error Handling

**Malformed JSON:**
- Server logs error to stderr
- Server does NOT send ack
- Device retries (per retry logic)

**Socket errors:**
- Server logs error to stderr
- Server attempts to continue operation
- Device reconnects when ready

**File write errors:**
- Server logs error to stderr
- Server attempts to continue (may skip that entry)

### Timeout Behavior

- **Socket timeout**: 30 seconds (configurable in code)
- **Ack timeout** (device side): 2 seconds, then retry
- **Retry backoff** (device side): 5s, 10s, 30s (exponential)

## Configuration

### Server Configuration

Edit `tcp_log_server.py` constants:
- `DEFAULT_PORT` - Default port (5000)
- `DEFAULT_HOST` - Default bind address (0.0.0.0)
- `DEFAULT_LOG_FILE` - Default log file path
- `SOCKET_TIMEOUT` - Socket timeout in seconds (30.0)
- `MAX_LINE_LENGTH` - Max bytes per line (16384)

### Device Configuration

Device configuration is set via `/data/config.json` on the ESP32:

```json
{
  "tcp_logging": {
    "server_host": "work-laptop.local",
    "server_port": 5000,
    "ack_timeout_ms": 2000,
    "heartbeat_interval_ms": 1000,
    "retry_backoff_ms": [5000, 10000, 30000]
  }
}
```

**Configuration fields:**
- `server_host` - Server hostname or IP (use `.local` mDNS hostname for laptops)
- `server_port` - Server port (default: 5000)
- `ack_timeout_ms` - Time to wait for ack before retry (default: 2000ms)
- `heartbeat_interval_ms` - Send heartbeat if idle for this duration (default: 1000ms)
- `retry_backoff_ms` - Exponential backoff delays for retries (e.g., [5000, 10000, 30000])

**mDNS hostname resolution:**
- Server binds to `0.0.0.0:5000` (listens on all network interfaces)
- Device connects to `work-laptop.local:5000` (mDNS hostname from config)
- ESP32 supports `.local` hostname resolution automatically via WiFi library
- Works seamlessly on local network without needing static IPs

See task-012 for device-side NetworkLogger implementation.

## Troubleshooting

### Server won't start: "Address already in use"

```bash
# Find process using port 5000
lsof -i :5000

# Kill the process
kill <PID>

# Or use a different port
python3 tcp_log_server.py --port 5001
```

### Device can't connect

- Verify server is running: `netstat -an | grep 5000`
- Verify firewall allows connections on port 5000
- Verify device and server are on same network
- Check device config.json has correct server IP

### No entries in log file

- Verify server started successfully (check stderr output)
- Verify device is sending data (check device serial output)
- Verify log file path is writable
- Check file permissions: `ls -l tcp_server.log`

### Entries appear corrupted

- Verify device is sending valid JSON-line format
- Check for network issues (packet loss, corruption)
- Verify device firmware is up to date

## Development

### Running Tests

```bash
# Start server in one terminal
python3 tcp_log_server.py --log-file test.log

# Run mock device in another terminal
python3 mock_device.py

# Verify output
cat test.log | jq | less
```

### Extending the Server

To add features:
1. Edit `tcp_log_server.py`
2. Add new command-line arguments in `main()`
3. Implement logic in `TCPLogServer` class
4. Test with mock device
5. Update this README

## Files

- `tcp_log_server.py` - Main TCP server implementation
- `mock_device.py` - Mock ESP32 device for testing
- `README.md` - This file
- `tcp_server.log` - Default output log file (gitignored)

## Related Tasks

- **task-007** - ESP32 Logger module (device-side logging to SPIFFS)
- **task-012** - ESP32 NetworkLogger task (device-side TCP sync)
- **task-011** - This server implementation

## License

Part of the Hydromatic project.

---

**Last Updated**: 2025-11-13
