# TimeManager Module - Test Plan

## Implementation Summary

The TimeManager module has been successfully implemented with the following features:

### Architecture Highlights
- **FreeRTOS-Safe**: Uses mutexes for thread-safe time access
- **Non-blocking NTP**: Implements timeout-based NTP sync using configTime()
- **Queue Integration**: Receives WiFi status events from wifiStatusQueue
- **Event Logging**: Circular buffer for debugging time sync operations

### Key Components

#### 1. TimeManager Class (src/time_manager.h / .cpp)
- **Thread Safety**: Mutex-protected shared state for `current_time`, `last_sync_time`, `confidence_state`
- **State Machine**: NTP sync states (IDLE, SYNCING, SUCCESS, FAILED)
- **Configuration**: Loads from config.json (ntp_server, timezone, sync_timeout_seconds, confidence_window_hours)

#### 2. FreeRTOS Integration (src/freertos_tasks.cpp)
- **timeTask**: Runs every 1000ms
  - Receives WiFi status from wifiStatusQueue
  - Calls TimeManager::handle() with current WiFi state
  - Handles NTP sync progression
- **Task Priority**: Same as WiFi task (priority 2) to ensure responsive sync
- **Stack Size**: 4KB (sufficient for NTP operations)

#### 3. Initialization (src/main.cpp)
- Global `TimeManager` instance created
- `timeManager.begin("/config.json")` called after WiFiManager in setup()
- TimeManager initialized before FreeRTOS tasks are created

### Configuration (data/config.json)
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

## Testing Plan

### Test 1: Initialization and Compilation
**Status**: ✅ PASSED
- Code compiles successfully with PlatformIO
- RAM usage: 22.5% (73,708 bytes / 327,680 bytes)
- Flash usage: 41.6% (818,469 bytes / 1,966,080 bytes)
- No compilation errors, only deprecation warnings in ArduinoJson usage

### Test 2: TimeManager Initialization on Device
**Status**: PENDING (Device testing required)
**Procedure**:
1. Flash firmware to ESP32 device
2. Open serial monitor at 115200 baud
3. Verify output:
   ```
   [SETUP] Initializing TimeManager...
   [TIME] TimeManager initializing...
   [TIME] TimeManager ready - awaiting WiFi for NTP sync
   ```
4. Verify no initialization errors

**Acceptance Criteria**:
- ✓ TimeManager initializes without crashes
- ✓ Compile-time fallback is set
- ✓ Configuration loaded from config.json
- ✓ Timezone applied to system

### Test 3: WiFi Connection Trigger NTP Sync
**Status**: PENDING (Device testing required)
**Procedure**:
1. Device connects to WiFi (logs `WiFi connected` event)
2. timeTask receives WiFi status
3. TimeManager enters NTP_SYNCING state
4. NTP sync completes within 5 seconds

**Expected Serial Output**:
```
[WiFiTask] State changed to: CONNECTED_STATION (CONNECTED)
[TimeTask] WiFi connected - TimeManager will attempt NTP sync
[TIME] Starting NTP sync with pool.ntp.org (timeout: 5s)
[TIME] NTP sync successful! Time: 2025-11-12 14:30:00
```

**Acceptance Criteria**:
- ✓ NTP sync initiated when WiFi connects
- ✓ System time updated with valid value
- ✓ Confidence state changes to TIME_CONFIDENT
- ✓ Last sync time recorded

### Test 4: Fallback on NTP Timeout (No WiFi)
**Status**: PENDING (Device testing required)
**Procedure**:
1. Device is offline (no WiFi)
2. Wait 5+ seconds after boot
3. Call `getTime()` method
4. Verify returns fallback compile-time value

**Expected Behavior**:
```
[TIME] Awaiting WiFi for NTP sync
[TimeTask] ... (timeout without WiFi)
[TIME] Status: YYYY-MM-DD HH:MM:SS (Unconfident)
```

**Acceptance Criteria**:
- ✓ Falls back to compile-time gracefully
- ✓ No crashes or hangs
- ✓ Confidence state remains TIME_UNCONFIDENT
- ✓ Retries NTP sync when WiFi becomes available later

### Test 5: Timezone Conversion
**Status**: PENDING (Device testing required)
**Procedure**:
1. Modify config.json timezone to "EST5EDT" (Eastern Time)
2. Force NTP sync with WiFi
3. Call both `getTime()` (UTC) and `getLocalTime()` (with TZ)
4. Verify difference is correct (UTC-5 for EST, UTC-4 for EDT)

**Test Code**:
```cpp
// In mainTask or via serial command
time_t utc = timeManager.getTime();
time_t local = timeManager.getLocalTime();
char utc_str[32], local_str[32];
timeManager.getTimeString(utc_str, sizeof(utc_str), "%Y-%m-%d %H:%M:%S", false); // UTC
timeManager.getTimeString(local_str, sizeof(local_str), "%Y-%m-%d %H:%M:%S", true); // Local
Serial.printf("UTC:   %s\n", utc_str);
Serial.printf("Local: %s\n", local_str);
```

**Acceptance Criteria**:
- ✓ UTC time is correct
- ✓ Local time reflects timezone offset
- ✓ Daylight saving time applied correctly (if applicable)

### Test 6: Time Confidence Expiration
**Status**: PENDING (Device testing required)
**Procedure**:
1. Sync time via NTP (confidence = CONFIDENT)
2. Wait or mock time to exceed confidence_window_hours (24 hours by default)
3. Call `isTimeConfident()`

**Acceptance Criteria**:
- ✓ Returns true immediately after sync
- ✓ Returns false after confidence window expires
- ✓ Allows re-sync when WiFi reconnects

### Test 7: Thread Safety (Mutex Protection)
**Status**: PENDING (Device stress testing)
**Procedure**:
1. Access time methods from multiple tasks simultaneously
2. Run for extended period (several hours)
3. Monitor for mutex contention warnings

**Acceptance Criteria**:
- ✓ No deadlocks or timeouts
- ✓ No time corruption from concurrent access
- ✓ All reads return consistent values

### Test 8: Event Logging
**Status**: PENDING (Device testing required)
**Procedure**:
1. Call `timeManager.printTimeLog()` in mainTask
2. Verify event log shows:
   - Initialization
   - Configuration loaded
   - NTP sync attempts
   - Success/failure results
   - Timezone changes

**Acceptance Criteria**:
- ✓ Circular buffer doesn't overflow
- ✓ Events logged in chronological order
- ✓ At least 50 entries can be stored

## Testing Environment
- **Device**: ESP32-DevKit-V4 (or compatible)
- **Firmware**: Latest compiled binary
- **Serial Monitor**: 115200 baud, 8N1
- **WiFi Network**: At least one network configured in config.json

## Success Criteria for Task Completion
All acceptance criteria from the task specification are met:
- ✅ TimeManager class created in src/time_manager.h/.cpp
- ✅ NTP sync on boot with 5 second timeout
- ✅ Time confidence tracking (CONFIDENT/UNCONFIDENT states)
- ✅ Timezone support via POSIX TZ string in config.json
- ✅ Fallback to compile-time on NTP failure or no WiFi
- ✅ Public interface: begin(), getTime(), getLocalTime(), isTimeConfident(), getLastSyncTime()
- ✅ Time sync events logged
- 🔄 Tested: NTP sync with WiFi, fallback without WiFi, timezone conversion (In Progress)

## Notes
- The module is designed to minimize blocking behavior for FreeRTOS
- NTP sync is non-blocking via configTime() + polling
- Mutex timeouts (100ms) ensure task fairness even under contention
- Event log can be extended to 200+ entries if needed by increasing MAX_LOG_ENTRIES
- Timezone support uses standard POSIX TZ format; DST transitions are handled by libc

## Follow-up Work (If Needed)
- Integration with task scheduling system to trigger immediate NTP on WiFi connect (optional optimization)
- Real-time clock (RTC) battery backup support (for when device is powered off)
- SNTP client customization for specific NTP pools (GPS, Stratum 1, etc.)
- Time sync event queue for other modules that need to react to time sync
