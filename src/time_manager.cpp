#include "time_manager.h"
#include "logger.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>

// ========================
// Constants
// ========================

const char* TimeManager::NTP_HISTORY_PATH = "/data/ntp_history.json";

// ========================
// Constructor
// ========================

TimeManager::TimeManager()
    : compile_time(0),
      current_time(0),
      last_sync_time(0),
      confidence_state(TIME_UNCONFIDENT),
      ntp_state(NTP_IDLE),
      ntp_attempt_time(0),
      ntp_attempt_counter(0),
      log_index(0) {
  // Create FreeRTOS mutex for thread-safe time access
  time_mutex = xSemaphoreCreateMutex();
  if (time_mutex == NULL) {
    Serial.println("[ERROR] TimeManager: Failed to create mutex!");
  }

  // Initialize buffers
  memset(ntp_server, 0, sizeof(ntp_server));
  memset(timezone, 0, sizeof(timezone));
  memset(time_log, 0, sizeof(time_log));

  // Default values
  ntp_timeout_seconds = 5;
  confidence_window_hours = 24;
  strncpy(ntp_server, "pool.ntp.org", sizeof(ntp_server) - 1);
  strncpy(timezone, "UTC0", sizeof(timezone) - 1);
}

// ========================
// Lifecycle Methods
// ========================

void TimeManager::begin(const char* configPath) {
  Serial.println("[TIME] TimeManager initializing...");

  // Set compile-time as fallback
  // __DATE__ and __TIME__ are provided by compiler
  // Parse to create a reasonable fallback time
  struct tm timeinfo = {};
  // Use a fixed fallback if we can't parse (2025-01-01 00:00:00)
  timeinfo.tm_year = 125;  // years since 1900
  timeinfo.tm_mon = 0;     // January
  timeinfo.tm_mday = 1;
  timeinfo.tm_hour = 0;
  timeinfo.tm_min = 0;
  timeinfo.tm_sec = 0;

  compile_time = mktime(&timeinfo);
  current_time = compile_time;

  logEvent("TimeManager initialized with compile-time fallback");

  // Load configuration from config.json
  loadConfig(configPath);

  // Apply timezone immediately
  setTimezone(timezone);

  // Load NTP history from file
  loadNTPHistory();

  // Log startup info
  logEventF("Config loaded: NTP=%s, TZ=%s, Timeout=%ds", ntp_server, timezone, ntp_timeout_seconds);
  Serial.println("[TIME] TimeManager ready - awaiting WiFi for NTP sync");
}

void TimeManager::handle(bool wifiIsConnected) {
  // Update NTP state machine
  updateNTPState();

  // If WiFi just became available and we're not synced, attempt NTP sync
  if (wifiIsConnected && confidence_state == TIME_UNCONFIDENT && ntp_state == NTP_IDLE) {
    logEvent("WiFi connected - initiating NTP sync");
    performNTPSync();
  }

  // If WiFi disconnected while syncing, abort
  if (!wifiIsConnected && ntp_state == NTP_SYNCING) {
    logEvent("WiFi disconnected - aborting NTP sync");
    ntp_state = NTP_IDLE;
  }
}

// ========================
// Configuration Loading
// ========================

void TimeManager::loadConfig(const char* configPath) {
  if (!SPIFFS.exists(configPath)) {
    logEvent("Config file not found - using defaults");
    return;
  }

  File configFile = SPIFFS.open(configPath, "r");
  if (!configFile) {
    logEvent("Failed to open config file");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    logEventF("Config JSON parse error: %s", error.c_str());
    return;
  }

  // Load time configuration section
  if (doc.containsKey("time")) {
    JsonObject timeConfig = doc["time"];

    if (timeConfig.containsKey("ntp_server")) {
      strncpy(ntp_server, timeConfig["ntp_server"], sizeof(ntp_server) - 1);
    }

    if (timeConfig.containsKey("timezone")) {
      strncpy(timezone, timeConfig["timezone"], sizeof(timezone) - 1);
    }

    if (timeConfig.containsKey("sync_timeout_seconds")) {
      ntp_timeout_seconds = timeConfig["sync_timeout_seconds"];
    }

    if (timeConfig.containsKey("confidence_window_hours")) {
      confidence_window_hours = timeConfig["confidence_window_hours"];
    }
  }

  logEvent("Configuration loaded successfully");
}

// ========================
// NTP Synchronization
// ========================

void TimeManager::performNTPSync() {
  if (ntp_state == NTP_SYNCING) {
    logEvent("NTP sync already in progress");
    return;
  }

  logEventF("Starting NTP sync with %s (timeout: %ds)", ntp_server, ntp_timeout_seconds);
  ntp_state = NTP_SYNCING;
  ntp_attempt_time = millis();
  ntp_attempt_counter++;

  // Use configTime for non-blocking NTP sync
  // Parameters: timezone_offset, DST_offset, NTP_server
  // We use UTC offset 0 here since we handle timezone separately
  configTime(0, 0, ntp_server);

  Serial.flush();
}

void TimeManager::updateNTPState() {
  if (ntp_state != NTP_SYNCING) {
    return;
  }

  // Check if time has been synchronized
  // On ESP32, time() returns 0 until NTP sync or manual set
  time_t now = time(nullptr);

  // Check if we got a valid time (year should be >= 2020)
  struct tm timeinfo = {};
  localtime_r(&now, &timeinfo);
  bool got_valid_time = (timeinfo.tm_year >= 120);  // 120 = 2020 - 1900

  unsigned long elapsed = millis() - ntp_attempt_time;
  unsigned long timeout_ms = ntp_timeout_seconds * 1000UL;

  if (got_valid_time) {
    // NTP sync successful!
    ntp_state = NTP_SUCCESS;

    // Update system time with mutex protection
    setSystemTime(now);

    logEventF("NTP sync successful! Time: %04d-%02d-%02d %02d:%02d:%02d",
        timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    // Protect shared state with mutex
    if (xSemaphoreTake(time_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      confidence_state = TIME_CONFIDENT;
      last_sync_time = now;
      xSemaphoreGive(time_mutex);
    }

    // Update NTP history for NetworkLogger timestamp calculation
    // Get current boot_seq from Logger
    uint32_t boot_seq = Logger::getInstance().getBootSeq();
    uint32_t uptime_ms = millis();
    updateNTPHistory(boot_seq, now, uptime_ms);

    // Reset for next potential sync
    ntp_state = NTP_IDLE;

  } else if (elapsed >= timeout_ms) {
    // Timeout reached without sync
    ntp_state = NTP_FAILED;
    logEventF("NTP sync timeout after %lu ms", elapsed);

    // Mark as unconfident, fall back to compile time
    if (xSemaphoreTake(time_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      confidence_state = TIME_UNCONFIDENT;
      current_time = compile_time;
      xSemaphoreGive(time_mutex);
    }

    // Reset for next attempt
    ntp_state = NTP_IDLE;
  }
  // else: still waiting for NTP response
}

// ========================
// Time Query Methods (Thread-Safe)
// ========================

time_t TimeManager::getTime() {
  time_t result = 0;

  if (xSemaphoreTake(time_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    // If confident, get current system time
    if (confidence_state == TIME_CONFIDENT) {
      result = time(nullptr);
    } else {
      // Otherwise use fallback
      result = compile_time;
    }
    xSemaphoreGive(time_mutex);
  } else {
    // Mutex timeout - return best-effort value
    result = time(nullptr);
    if (result == 0) {
      result = compile_time;
    }
  }

  return result;
}

time_t TimeManager::getLocalTime() {
  // Get UTC time
  time_t utc_time = getTime();

  // Convert to local time by applying timezone offset
  // Note: The timezone conversion is handled by struct tm conversion
  // We return the same time_t but when displayed via localtime(), it shows local time
  return utc_time;
}

struct tm* TimeManager::getTimeInfo(struct tm* timeinfo) {
  if (!timeinfo) {
    return nullptr;
  }

  time_t t = getTime();
  return gmtime_r(&t, timeinfo);
}

struct tm* TimeManager::getLocalTimeInfo(struct tm* timeinfo) {
  if (!timeinfo) {
    return nullptr;
  }

  time_t t = getLocalTime();
  return localtime_r(&t, timeinfo);
}

size_t TimeManager::getTimeString(char* buffer, size_t size, const char* format, bool useLocal) {
  if (!buffer || size == 0) {
    return 0;
  }

  struct tm timeinfo = {};
  struct tm* result;

  if (useLocal) {
    result = getLocalTimeInfo(&timeinfo);
  } else {
    result = getTimeInfo(&timeinfo);
  }

  if (!result) {
    return 0;
  }

  return strftime(buffer, size, format, &timeinfo);
}

// ========================
// Confidence State Methods
// ========================

bool TimeManager::isTimeConfident() {
  bool result = false;

  if (xSemaphoreTake(time_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    result = (confidence_state == TIME_CONFIDENT);

    // Check if confidence has expired (older than confidence_window_hours)
    if (result && confidence_window_hours > 0) {
      time_t now = time(nullptr);
      uint32_t hours_since_sync = (now - last_sync_time) / 3600;
      if (hours_since_sync > confidence_window_hours) {
        result = false;  // Confidence expired
      }
    }

    xSemaphoreGive(time_mutex);
  }

  return result;
}

TimeConfidenceState TimeManager::getConfidenceState() {
  TimeConfidenceState result = TIME_UNCONFIDENT;

  if (xSemaphoreTake(time_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    result = confidence_state;
    xSemaphoreGive(time_mutex);
  }

  return result;
}

time_t TimeManager::getLastSyncTime() {
  time_t result = 0;

  if (xSemaphoreTake(time_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    result = last_sync_time;
    xSemaphoreGive(time_mutex);
  }

  return result;
}

uint32_t TimeManager::getMillisSinceSyncTime() {
  uint32_t result = UINT32_MAX;

  if (xSemaphoreTake(time_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (last_sync_time > 0) {
      time_t now = time(nullptr);
      result = (now - last_sync_time) * 1000;
    }
    xSemaphoreGive(time_mutex);
  }

  return result;
}

// ========================
// Timezone Methods
// ========================

const char* TimeManager::getTimezone() {
  return timezone;
}

void TimeManager::setTimezone(const char* tz) {
  if (!tz || strlen(tz) == 0) {
    return;
  }

  strncpy(timezone, tz, sizeof(timezone) - 1);

  // Apply timezone to system environment
  setenv("TZ", tz, 1);
  tzset();  // Update timezone info

  logEventF("Timezone set to: %s", tz);
}

// ========================
// System Time Update
// ========================

void TimeManager::setSystemTime(time_t newTime) {
  struct timeval tv;
  tv.tv_sec = newTime;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);

  if (xSemaphoreTake(time_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    current_time = newTime;
    xSemaphoreGive(time_mutex);
  }
}

// ========================
// Logging Methods
// ========================

void TimeManager::logEvent(const char* message) {
  if (!message) {
    return;
  }

  // Circular buffer write
  LogEntry& entry = time_log[log_index];
  entry.timestamp = millis();
  strncpy(entry.message, message, sizeof(entry.message) - 1);
  entry.message[sizeof(entry.message) - 1] = '\0';

  log_index = (log_index + 1) % MAX_LOG_ENTRIES;
}

void TimeManager::logEventF(const char* format, ...) {
  if (!format) {
    return;
  }

  char buffer[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  logEvent(buffer);
}

void TimeManager::printStatus() {
  char timeStr[32];
  getTimeString(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", true);

  const char* confidence = isTimeConfident() ? "Confident" : "Unconfident";

  Serial.print("[TIME] Status: ");
  Serial.print(timeStr);
  Serial.print(" (");
  Serial.print(confidence);
  Serial.println(")");

  if (last_sync_time > 0) {
    struct tm lastSync = {};
    localtime_r(&last_sync_time, &lastSync);
    Serial.printf("[TIME] Last sync: %04d-%02d-%02d %02d:%02d:%02d\n",
        lastSync.tm_year + 1900, lastSync.tm_mon + 1, lastSync.tm_mday,
        lastSync.tm_hour, lastSync.tm_min, lastSync.tm_sec);
  } else {
    Serial.println("[TIME] Last sync: Never");
  }

  Serial.printf("[TIME] NTP attempts: %lu\n", ntp_attempt_counter);
}

void TimeManager::printTimeLog() {
  Serial.println("\n=== TIME SYNC EVENT LOG ===");
  Serial.printf("Log entries: %d/%d\n", getLogEntryCount(), MAX_LOG_ENTRIES);

  // Find first valid entry
  int entries = 0;
  for (int i = 0; i < MAX_LOG_ENTRIES; i++) {
    if (time_log[i].timestamp > 0 || time_log[i].message[0] != '\0') {
      entries++;
    }
  }

  // Print in chronological order
  for (int i = 0; i < entries; i++) {
    int index = (log_index + i) % MAX_LOG_ENTRIES;
    LogEntry& entry = time_log[index];

    if (entry.message[0] != '\0') {
      Serial.printf("[%6lu ms] %s\n", entry.timestamp, entry.message);
    }
  }
}

int TimeManager::getLogEntryCount() {
  int count = 0;
  for (int i = 0; i < MAX_LOG_ENTRIES; i++) {
    if (time_log[i].message[0] != '\0') {
      count++;
    }
  }
  return count;
}

// ========================
// Helper Methods
// ========================

const char* TimeManager::getConfidenceString(TimeConfidenceState state) {
  switch (state) {
    case TIME_CONFIDENT:
      return "CONFIDENT";
    case TIME_UNCONFIDENT:
      return "UNCONFIDENT";
    default:
      return "UNKNOWN";
  }
}

const char* TimeManager::getNTPStateString(NTPSyncState state) {
  switch (state) {
    case NTP_IDLE:
      return "IDLE";
    case NTP_SYNCING:
      return "SYNCING";
    case NTP_SUCCESS:
      return "SUCCESS";
    case NTP_FAILED:
      return "FAILED";
    default:
      return "UNKNOWN";
  }
}

// ========================
// NTP History Management (for NetworkLogger)
// ========================

void TimeManager::loadNTPHistory() {
  if (!SPIFFS.exists(NTP_HISTORY_PATH)) {
    Serial.println("[TIME] NTP history file not found, will create on first sync");
    return;
  }

  File file = SPIFFS.open(NTP_HISTORY_PATH, FILE_READ);
  if (!file) {
    Serial.println("[TIME] Failed to open NTP history file");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.printf("[TIME] NTP history JSON parse error: %s, reinitializing\n", error.c_str());
    // Corrupted file - reinitialize
    return;
  }

  if (!doc.containsKey("boots")) {
    Serial.println("[TIME] NTP history missing 'boots' array, reinitializing");
    return;
  }

  JsonArray boots = doc["boots"];
  Serial.printf("[TIME] Loaded NTP history with %d boot entries\n", boots.size());
}

void TimeManager::updateNTPHistory(uint32_t boot_seq, time_t ntp_sync_time, uint32_t sync_uptime_ms) {
  // Read existing history
  JsonDocument doc;

  if (SPIFFS.exists(NTP_HISTORY_PATH)) {
    File file = SPIFFS.open(NTP_HISTORY_PATH, FILE_READ);
    if (file) {
      DeserializationError error = deserializeJson(doc, file);
      file.close();

      if (error) {
        Serial.printf("[TIME] NTP history corrupted, reinitializing: %s\n", error.c_str());
        doc.clear();
      }
    }
  }

  // Ensure boots array exists
  if (!doc.containsKey("boots")) {
    doc["boots"] = JsonArray();
  }

  JsonArray boots = doc["boots"];

  // Check if this boot_seq already exists (update if so)
  bool found = false;
  for (JsonObject boot : boots) {
    if (boot["boot_seq"] == boot_seq) {
      boot["ntp_sync_time"] = (unsigned long)ntp_sync_time;
      boot["sync_uptime_ms"] = sync_uptime_ms;
      found = true;
      break;
    }
  }

  // Add new entry if not found
  if (!found) {
    JsonObject newBoot = boots.add<JsonObject>();
    newBoot["boot_seq"] = boot_seq;
    newBoot["ntp_sync_time"] = (unsigned long)ntp_sync_time;
    newBoot["sync_uptime_ms"] = sync_uptime_ms;
  }

  // Prune old entries (keep last MAX_BOOT_HISTORY)
  while (boots.size() > MAX_BOOT_HISTORY) {
    boots.remove(0);  // Remove oldest
  }

  // Write back to file
  File file = SPIFFS.open(NTP_HISTORY_PATH, FILE_WRITE);
  if (!file) {
    Serial.println("[TIME] Failed to open NTP history file for writing");
    return;
  }

  size_t written = serializeJson(doc, file);
  file.close();

  if (written == 0) {
    Serial.println("[TIME] Failed to write NTP history");
  } else {
    Serial.printf("[TIME] NTP history updated: boot_seq=%u, sync_time=%lu, uptime=%u (%zu bytes)\n",
                  boot_seq, (unsigned long)ntp_sync_time, sync_uptime_ms, written);
  }
}

bool TimeManager::getNTPHistoryForBoot(uint32_t boot_seq, time_t& ntp_sync_time, uint32_t& sync_uptime_ms) {
  // Initialize outputs
  ntp_sync_time = 0;
  sync_uptime_ms = 0;

  if (!SPIFFS.exists(NTP_HISTORY_PATH)) {
    return false;
  }

  File file = SPIFFS.open(NTP_HISTORY_PATH, FILE_READ);
  if (!file) {
    Serial.println("[TIME] getNTPHistoryForBoot: Failed to open NTP history file");
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.printf("[TIME] getNTPHistoryForBoot: JSON parse error: %s\n", error.c_str());
    return false;
  }

  if (!doc.containsKey("boots")) {
    return false;
  }

  JsonArray boots = doc["boots"];
  for (JsonObject boot : boots) {
    if (boot["boot_seq"] == boot_seq) {
      // Found matching boot_seq
      ntp_sync_time = boot["ntp_sync_time"];
      sync_uptime_ms = boot["sync_uptime_ms"];
      return true;
    }
  }

  // Not found
  return false;
}
