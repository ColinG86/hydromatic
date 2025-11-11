# Hydromatic - Functionality Specification
## Phase 1: WiFi, OTA, and Time Sync

**Created:** 2025-11-11
**Status:** Design Phase
**Priority:** Critical (blocking all remote operations)

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│ WiFi Manager                                                │
│ ├─ Station Mode (connect to existing WiFi network)         │
│ ├─ AP Mode (fallback if station fails)                     │
│ ├─ Credential management (load/save from JSON config)      │
│ └─ Connection retry logic                                  │
└─────────────────────────────────────────────────────────────┘
                          ↕
┌─────────────────────────────────────────────────────────────┐
│ Time Sync Manager                                           │
│ ├─ NTP synchronization (on boot)                           │
│ ├─ Timezone handling                                       │
│ ├─ Time confidence tracking                                │
│ └─ Fallback to offline time (no sync on WiFi loss)        │
└─────────────────────────────────────────────────────────────┘
                          ↕
┌─────────────────────────────────────────────────────────────┐
│ OTA Manager                                                 │
│ ├─ ArduinoOTA integration (WiFi push from IDE)             │
│ ├─ Update status tracking                                  │
│ └─ Recovery on update failure                              │
└─────────────────────────────────────────────────────────────┘
```

---

## 1. WiFi Manager Specification

### 1.1 Core Functionality

**Purpose:** Establish and maintain WiFi network connectivity with graceful fallback to AP mode

#### 1.1.1 Station Mode (Primary)
- **Input:** SSID and password from `/data/config.json`
- **Process:**
  1. Load credentials from config (see 1.4 for format)
  2. Attempt connection to specified SSID
  3. Wait for IP address assignment (timeout: 10 seconds)
  4. Verify connectivity (ping or HTTP request)
  5. Log connection status
- **Output:** Connected to WiFi with valid IP address
- **Success Criteria:**
  - `WiFi.status() == WL_CONNECTED`
  - Valid IP address assigned (not 0.0.0.0)
  - Can reach external host (e.g., pool.ntp.org)

#### 1.1.2 AP Mode (Fallback)
- **Trigger:** If station mode fails after 3 consecutive attempts
- **SSID:** `hydromatic-XXXXXX` (last 6 chars of MAC address for uniqueness)
- **Password:** Stored in `/data/config.json` under `wifi.ap_password`
- **IP Address:** 192.168.4.1 (standard ESP32 AP IP)
- **Features:**
  - DNS spoofing (all DNS queries resolve to 192.168.4.1)
  - Web interface/telnet accessible via AP
  - Allows user to reconfigure WiFi credentials via HTTP interface (future feature)
- **Success Criteria:**
  - AP mode broadcasts SSID
  - Clients can connect and receive IP via DHCP (192.168.4.2 - 192.168.4.10)
  - DNS resolution works from connected clients

#### 1.1.3 Credential Management
- **Storage:** JSON config file at `/data/config.json`
- **Format:** (see 1.4)
- **Operations:**
  - Load: On startup, read from config
  - Update: Via command interface (future, not in Phase 1)
  - Persist: Already in file, no runtime changes yet
- **Fallback:** If config missing, use compiled defaults (optional safety net)

#### 1.1.4 Connection State Tracking
- **State Machine:**
  ```
  DISCONNECTED
    ↓ (begin() called)
  CONNECTING_STATION
    ├─ (success) → CONNECTED_STATION
    └─ (fail 3x) → STARTING_AP
  STARTING_AP → CONNECTED_AP
  CONNECTED_STATION
    └─ (disconnected) → CONNECTING_STATION
  CONNECTED_AP
    └─ (no change, sticky)
  ```
- **Status Queries:**
  - `isConnected()` - returns true if either CONNECTED_STATION or CONNECTED_AP
  - `getMode()` - returns STATION_MODE or AP_MODE
  - `getSignalStrength()` - returns RSSI in dBm (station mode only)
  - `getLocalIP()` - returns current IP address
  - `getMACAddress()` - returns device MAC address

#### 1.1.5 Error Handling
- **Connection failures:** Log and retry up to 3 times (30 second total wait)
- **AP mode transition:** Log and notify (via activity logger when available)
- **Credential errors:** Detect invalid config, log, fall back to AP mode
- **Network loss:** Attempt to reconnect every 5 seconds

### 1.2 Configuration

**File:** `/data/config.json`

```json
{
  "wifi": {
    "station": {
      "ssid": "YourNetworkName",
      "password": "YourNetworkPassword"
    },
    "ap": {
      "password": "hydromatic_default_password",
      "ssid_prefix": "hydromatic"
    },
    "retry_attempts": 3,
    "retry_interval_ms": 10000,
    "reconnect_interval_ms": 5000
  }
}
```

### 1.3 Public Interface

```cpp
class WiFiManager {
public:
  // Lifecycle
  void begin();

  // Status queries
  bool isConnected();
  uint8_t getMode();  // STATION_MODE or AP_MODE
  String getLocalIP();
  String getMACAddress();
  int8_t getSignalStrength();  // RSSI, station mode only

  // Utility
  String getModeString();  // "Station" or "AP"
  void reconnect();  // Force reconnection attempt
  void printStatus();  // Debug output
};
```

### 1.4 Dependencies
- Arduino WiFi library (built-in ESP32)
- ArduinoJson (already in project for config parsing)
- Logger (for logging connection events)

### 1.5 Testing Requirements
- [ ] Station mode connection to real WiFi network
- [ ] Station mode connection timeout handling
- [ ] AP mode activation after station mode failure
- [ ] AP mode DHCP functionality (verify client can get IP)
- [ ] AP mode DNS spoofing (all queries resolve to 192.168.4.1)
- [ ] Connection state machine transitions
- [ ] Signal strength reading in station mode
- [ ] Manual reconnection via `reconnect()`

---

## 2. Time Sync Manager Specification

### 2.1 Core Functionality

**Purpose:** Synchronize device time via NTP at boot, establish time confidence, handle offline degradation

#### 2.1.1 NTP Synchronization (Boot)
- **Trigger:** Called from `main.cpp` during initialization (after WiFi connected or timeout)
- **Server:** Configurable in `/data/config.json`, default: `pool.ntp.org`
- **Process:**
  1. If WiFi connected: attempt NTP sync (timeout: 5 seconds)
  2. If successful: set system time, mark time as "confident"
  3. If failed (no WiFi or timeout): use fallback time (see 2.1.2)
  4. Log result with timestamp
- **Output:** System time set to NTP value OR fallback value
- **Success Criteria:**
  - `time(nullptr)` returns epoch seconds > 1700000000 (after Nov 2023)
  - Time zone offset applied correctly
  - Can be queried via `getTime()`

#### 2.1.2 Fallback Time (Offline Operation)
- **Trigger:** If NTP sync fails and WiFi unavailable
- **Behavior:**
  - Use compile-time timestamp (firmware build time)
  - Mark time as "unconfident"
  - Return to cycling with "light off" default state
  - Will accept time updates when WiFi restored (future enhancement)
- **Note:** Does NOT include internal RTC fallback in Phase 1 (can add later if hardware available)

#### 2.1.3 Time Confidence Tracking
- **States:**
  - `CONFIDENT`: Time from NTP, within 24 hours
  - `UNCONFIDENT`: Time from fallback or older than 24 hours
- **Use Cases:**
  - Cycle manager checks confidence before executing timed cycles
  - Log entries use confidence flag to indicate timestamp reliability
  - Schedule manager requires confident time for precise scheduling
- **Status Query:** `isTimeConfident()`

#### 2.1.4 Timezone Support
- **Configuration:** Via `/data/config.json` (POSIX TZ string)
- **Example:** `"TZ": "EST5EDT,M3.2.0,M11.1.0"` for US Eastern
- **Applied:** On boot via `setenv()` and `tzset()`
- **Verification:** Local time conversion can be tested

#### 2.1.5 Time Queries
- **`getTime()`** - returns epoch seconds (UTC)
- **`getLocalTime()`** - returns human-readable local time string
- **`isTimeConfident()`** - returns confidence state
- **`getLastSyncTime()`** - returns epoch of last successful NTP sync
- **`getTimeSinceSync()`** - returns seconds since last sync

### 2.2 Configuration

**File:** `/data/config.json`

```json
{
  "time": {
    "ntp_server": "pool.ntp.org",
    "timezone": "UTC0",
    "sync_timeout_seconds": 5,
    "confidence_window_hours": 24
  }
}
```

### 2.3 Public Interface

```cpp
class TimeManager {
public:
  // Lifecycle
  void begin();

  // Time access
  time_t getTime();  // epoch seconds (UTC)
  String getLocalTime();  // human-readable local time

  // Confidence
  bool isTimeConfident();
  time_t getLastSyncTime();
  uint32_t getTimeSinceSync();

  // Manual sync (future)
  void syncNTP();  // called during cycling phase

  // Utility
  void setTimezone(String tz);
  void printStatus();
};
```

### 2.4 Dependencies
- Arduino WiFi library (for NTP)
- WiFiManager (must be connected for NTP to work)
- Logger (for logging sync events)

### 2.5 Testing Requirements
- [ ] NTP sync on boot with WiFi available
- [ ] Fallback to compile-time on NTP timeout
- [ ] Fallback to compile-time with no WiFi
- [ ] Timezone conversion (verify local time is correct)
- [ ] Time confidence flag set correctly
- [ ] Time queries return valid epochs
- [ ] Offline operation (time doesn't sync but continues)

---

## 3. OTA Manager Specification

### 3.1 Core Functionality

**Purpose:** Enable wireless firmware updates via ArduinoOTA when WiFi connected

#### 3.1.1 OTA Server Initialization
- **Trigger:** Called from `main.cpp` after WiFi manager and Time manager initialized
- **Process:**
  1. Check if WiFi connected
  2. If yes: start ArduinoOTA server on standard port (default 3232)
  3. If no: defer until WiFi available (not critical for Phase 1)
  4. Set hostname for easier IDE discovery
  5. Register callback handlers (start, end, progress, error)
- **Hostname:** `hydromatic-XXXXXX` (last 6 chars of MAC address)
- **Output:** OTA server listening and discoverable from Arduino IDE
- **Success Criteria:**
  - Appears in Arduino IDE "Upload via Network" dropdown
  - Can accept firmware file and complete upload
  - Device restarts with new firmware

#### 3.1.2 Upload Progress Tracking
- **Callbacks:**
  - `onStart()` - log update start, disable critical operations
  - `onProgress(current, total)` - log percentage (every 10% or less)
  - `onEnd()` - log update complete
  - `onError(error)` - log error code and reason
- **Logging:** All events logged via Logger module
- **LED/Status:** (future enhancement) could blink indicator during upload

#### 3.1.3 Error Handling
- **Auth failure:** Log and reject upload
- **Insufficient space:** Detect and log
- **Write failure:** Log and restart
- **Timeout:** Arduino framework handles (standard 15-30s timeout)
- **Recovery:** Device remains functional after failed update (no rollback needed for Phase 1)

#### 3.1.4 Status Queries
- **`isEnabled()`** - returns true if OTA server running
- **`isUpdating()`** - returns true during active upload
- **`getProgress()`** - returns 0-100 percent (only during upload)
- **`getStatus()`** - returns status string for display

### 3.2 Configuration

**File:** `/data/config.json`

```json
{
  "ota": {
    "enabled": true,
    "port": 3232,
    "hostname_prefix": "hydromatic"
  }
}
```

### 3.3 Public Interface

```cpp
class OTAManager {
public:
  // Lifecycle
  void begin();
  void handle();  // called from main loop to process OTA events

  // Status
  bool isEnabled();
  bool isUpdating();
  uint8_t getProgress();  // 0-100
  String getStatus();

  // Utility
  void printStatus();
};
```

### 3.4 Dependencies
- WiFiManager (must be connected)
- TimeManager (for logging timestamps)
- Logger (for logging events)
- Arduino built-in: ArduinoOTA library

### 3.5 Testing Requirements
- [ ] OTA server starts after WiFi connected
- [ ] Device appears in Arduino IDE network uploads
- [ ] Successful firmware upload and restart
- [ ] Progress callbacks fire during upload
- [ ] Error callbacks fire on auth failure
- [ ] Device continues functioning if OTA disabled
- [ ] Hostname is discoverable

---

## 4. Cross-Module Integration

### 4.1 Initialization Order (in `main.cpp`)

```
1. Serial.begin()           // Debug output
2. Logger.begin()           // Logging system
3. WiFiManager.begin()      // Must be first for network ops
4. TimeManager.begin()      // Uses WiFi if available
5. OTAManager.begin()       // Requires WiFi + Time
```

**Rationale:**
- Logger must be ready first to capture all events
- WiFi enables Time and OTA
- Time needed for accurate OTA timestamps

### 4.2 Main Loop Integration

```cpp
void loop() {
  OTAManager.handle();  // Check for pending OTA updates

  // Rest of main loop (future phases)
  // - Sensor reads
  // - Cycle management
  // - Scheduling
  // - Command handling
}
```

### 4.3 JSON Configuration File Structure

**File Location:** `/data/config.json`

**Format:**
```json
{
  "wifi": {
    "station": {
      "ssid": "YourNetwork",
      "password": "YourPassword"
    },
    "ap": {
      "password": "default_ap_password",
      "ssid_prefix": "hydromatic"
    },
    "retry_attempts": 3,
    "retry_interval_ms": 10000,
    "reconnect_interval_ms": 5000
  },
  "time": {
    "ntp_server": "pool.ntp.org",
    "timezone": "UTC0",
    "sync_timeout_seconds": 5,
    "confidence_window_hours": 24
  },
  "ota": {
    "enabled": true,
    "port": 3232,
    "hostname_prefix": "hydromatic"
  }
}
```

---

## 5. Success Criteria (Phase 1 Complete)

- [ ] WiFi connects to configured network (or starts AP mode on failure)
- [ ] Time synchronizes via NTP when WiFi available
- [ ] OTA updates work via Arduino IDE network upload
- [ ] Device continues operating if WiFi drops
- [ ] All events logged with timestamps
- [ ] Configuration loaded from `/data/config.json`
- [ ] Can be controlled remotely (via future command handler)

---

## 6. Potential Issues & Mitigation

| Issue | Symptom | Mitigation |
|-------|---------|-----------|
| WiFi credential typo | Station mode fails, endless AP mode | Log credential load, validate SSID format |
| NTP timeout on slow network | Startup delay 5+ seconds | Make timeout configurable, make it non-blocking (future) |
| OTA during critical operation | Device restarting mid-cycle | Disable OTA during cycles (future enhancement) |
| Time jump (large correction) | Logs out of order | Time confidence flag, warn in logs |
| Config file corrupted | System fails to boot | Validate JSON, provide defaults |
| WiFi drops during OTA | Incomplete upload | OTA framework handles via timeout/retry |

---

## 7. Future Enhancements (Not Phase 1)

- [ ] WebUI on AP mode for WiFi credential configuration
- [ ] Multiple preferred WiFi networks with priority
- [ ] mDNS (Bonjour) for device discovery without knowing IP
- [ ] Periodic NTP re-sync during operation (not just boot)
- [ ] HTTP OTA server polling (alternative to Arduino IDE)
- [ ] Time update via command interface
- [ ] LED status indicator during OTA
- [ ] Graceful shutdown of cycles before OTA restart
- [ ] Firmware rollback on failed update

---

## Notes for Implementation

1. **Keep it modular:** Each manager (WiFi, Time, OTA) should be independent with clear interfaces
2. **Log everything:** All state changes, errors, and timeouts should be logged
3. **Test offline:** Verify system works with no WiFi (time unconfident, cycles disabled)
4. **Test AP mode:** Ensure AP fallback works, clients can connect
5. **Test OTA:** Upload from IDE, verify firmware updates
6. **Configuration-driven:** All parameters in config.json, no magic numbers

