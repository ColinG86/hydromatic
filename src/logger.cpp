#include "logger.h"
#include <vector>
#include <cstdio>
#include <cstdarg>

// ========================
// Constants
// ========================

const char* Logger::LOG_FILE_PATH = "/data/active.log";
const char* Logger::BOOT_COUNTER_PATH = "/data/boot_counter.json";
const char* Logger::DATA_DIR_PATH = "/data";
const float Logger::ROTATION_THRESHOLD = 0.8f;

// ========================
// Singleton Access
// ========================

Logger& Logger::getInstance() {
  static Logger instance;
  return instance;
}

// ========================
// Constructor (Private)
// ========================

Logger::Logger()
    : boot_seq(0),
      entry_seq(0),
      log_mutex(nullptr),
      initialized(false) {
  // Constructor intentionally minimal to avoid FreeRTOS conflicts during static init
  // Mutex will be created in begin() after FreeRTOS is fully initialized
}

// ========================
// Lifecycle Methods
// ========================

void Logger::begin() {
  Serial.println("[LOGGER] Initializing Logger subsystem...");

  // Create FreeRTOS mutex for thread-safe file access
  // Done here (not in constructor) to ensure FreeRTOS is fully initialized
  if (log_mutex == nullptr) {
    log_mutex = xSemaphoreCreateMutex();
    if (log_mutex == nullptr) {
      Serial.println("[LOGGER] ERROR: Failed to create mutex! File writes may not be thread-safe.");
      // Continue anyway - single-threaded operation will still work
    } else {
      Serial.println("[LOGGER] Mutex created successfully");
    }
  }

  // Ensure /data/ directory exists
  if (!SPIFFS.exists(DATA_DIR_PATH)) {
    Serial.println("[LOGGER] Creating /data/ directory...");
    if (!SPIFFS.mkdir(DATA_DIR_PATH)) {
      Serial.println("[LOGGER] WARNING: Failed to create /data/ directory");
      // Continue anyway - may already exist or SPIFFS may auto-create
    }
  }

  // Read boot counter from persistent storage
  boot_seq = readBootCounter();
  Serial.printf("[LOGGER] Previous boot_seq: %u\n", boot_seq);

  // Increment boot sequence for this boot
  boot_seq++;
  Serial.printf("[LOGGER] Current boot_seq: %u\n", boot_seq);

  // Write updated boot counter back to storage
  writeBootCounter(boot_seq);

  // Reset entry sequence for this boot
  entry_seq = 0;

  // Mark as initialized
  initialized = true;

  // Log startup message
  logInfo("Logger initialized, boot_seq=%u", boot_seq);
}

// ========================
// Internal: Boot Counter Persistence
// ========================

uint32_t Logger::readBootCounter() {
  if (!SPIFFS.exists(BOOT_COUNTER_PATH)) {
    Serial.println("[LOGGER] Boot counter file not found, initializing to 0");
    return 0;
  }

  File file = SPIFFS.open(BOOT_COUNTER_PATH, FILE_READ);
  if (!file) {
    Serial.println("[LOGGER] Failed to open boot counter file, initializing to 0");
    return 0;
  }

  // Parse JSON: {"boot_seq": N}
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.printf("[LOGGER] Boot counter JSON parse error: %s, initializing to 0\n", error.c_str());
    return 0;
  }

  if (!doc.containsKey("boot_seq")) {
    Serial.println("[LOGGER] boot_seq field missing in boot counter, initializing to 0");
    return 0;
  }

  uint32_t seq = doc["boot_seq"];
  Serial.printf("[LOGGER] Read boot_seq from file: %u\n", seq);
  return seq;
}

void Logger::writeBootCounter(uint32_t seq) {
  // Create JSON document: {"boot_seq": N}
  StaticJsonDocument<256> doc;
  doc["boot_seq"] = seq;

  // Open file for writing (overwrites existing)
  File file = SPIFFS.open(BOOT_COUNTER_PATH, FILE_WRITE);
  if (!file) {
    Serial.println("[LOGGER] ERROR: Failed to open boot counter file for writing");
    return;
  }

  // Serialize JSON to file
  size_t written = serializeJson(doc, file);
  file.close();

  if (written == 0) {
    Serial.println("[LOGGER] ERROR: Failed to write boot counter JSON");
  } else {
    Serial.printf("[LOGGER] Boot counter written: boot_seq=%u (%zu bytes)\n", seq, written);
  }
}

// ========================
// Public Logging Methods
// ========================

void Logger::logDebug(const char* fmt, ...) {
  if (!initialized) return;

  va_list args;
  va_start(args, fmt);
  log("debug", fmt, args);
  va_end(args);
}

void Logger::logInfo(const char* fmt, ...) {
  if (!initialized) return;

  va_list args;
  va_start(args, fmt);
  log("info", fmt, args);
  va_end(args);
}

void Logger::logWarning(const char* fmt, ...) {
  if (!initialized) return;

  va_list args;
  va_start(args, fmt);
  log("warning", fmt, args);
  va_end(args);
}

void Logger::logError(const char* fmt, ...) {
  if (!initialized) return;

  va_list args;
  va_start(args, fmt);
  log("error", fmt, args);
  va_end(args);
}

// ========================
// Core Logging Implementation
// ========================

void Logger::log(const char* level, const char* fmt, va_list args) {
  // Format message with vsnprintf
  char message[MAX_MESSAGE_SIZE + 1];
  int len = vsnprintf(message, sizeof(message), fmt, args);

  // Check if message was truncated
  bool truncated = false;
  if (len >= (int)MAX_MESSAGE_SIZE) {
    message[MAX_MESSAGE_SIZE] = '\0';
    truncated = true;
  }

  // Get system stats (this also triggers rotation check)
  StaticJsonDocument<1024> stats_doc;
  getSystemStats(stats_doc);

  // Build JSON-line entry
  StaticJsonDocument<2048> entry_doc;
  entry_doc["boot_seq"] = boot_seq;
  entry_doc["uptime_ms"] = millis();
  entry_doc["seq"] = entry_seq;
  entry_doc["level"] = level;
  entry_doc["msg"] = message;
  entry_doc["system"] = stats_doc;  // Nest system stats

  // Serialize to string
  String json_line;
  serializeJson(entry_doc, json_line);
  json_line += "\n";

  // Append to log file (mutex-protected)
  appendToLog(json_line);

  // Output to Serial for boot debugging
  Serial.print("[");
  Serial.print(level);
  Serial.print("] ");
  Serial.println(message);

  // Increment sequence counter
  entry_seq++;

  // If message was truncated, log an error (but avoid infinite recursion)
  if (truncated && strcmp(level, "error") != 0) {
    // Create a small truncated sample for the error message
    char sample[64];
    strncpy(sample, message, sizeof(sample) - 4);
    sample[sizeof(sample) - 4] = '.';
    sample[sizeof(sample) - 3] = '.';
    sample[sizeof(sample) - 2] = '.';
    sample[sizeof(sample) - 1] = '\0';

    logError("Log entry truncated (%d chars), sample: %s", len, sample);
  }
}

// ========================
// System Stats Collection
// ========================

void Logger::getSystemStats(JsonDocument& doc) {
  // Heap stats
  doc["heap_free"] = ESP.getFreeHeap();
  doc["heap_used"] = ESP.getHeapSize() - ESP.getFreeHeap();

  // PSRAM stats (may be 0 if no PSRAM available)
  doc["free_psram"] = ESP.getFreePsram();

  // Uptime
  doc["uptime_ms"] = millis();

  // Task count
  doc["task_count"] = uxTaskGetNumberOfTasks();

  // SPIFFS stats
  size_t total, used;
  getSPIFFSInfo(total, used);
  doc["spiffs_free"] = total - used;
  doc["spiffs_used"] = used;

  // Trigger rotation check after gathering stats
  checkAndRotateLog();
}

void Logger::getSPIFFSInfo(size_t& total, size_t& used) {
  total = SPIFFS.totalBytes();
  used = SPIFFS.usedBytes();
}

// ========================
// Log Rotation
// ========================

void Logger::checkAndRotateLog() {
  // Get SPIFFS capacity
  size_t total, used;
  getSPIFFSInfo(total, used);

  // Check if log file exists
  if (!SPIFFS.exists(LOG_FILE_PATH)) {
    return;  // Log doesn't exist yet, nothing to rotate
  }

  // Get log file size
  File f = SPIFFS.open(LOG_FILE_PATH, FILE_READ);
  if (!f) {
    return;  // Failed to open log
  }
  size_t log_size = f.size();
  f.close();

  // Check if rotation is needed (log > 80% of total SPIFFS)
  if (log_size > (size_t)(total * ROTATION_THRESHOLD)) {
    // Prune to 50% of threshold (leaves room to grow)
    size_t target = (size_t)(total * ROTATION_THRESHOLD) / 2;
    Serial.printf("[LOGGER] Log size %zu exceeds threshold (%.0f%% of %zu), pruning to %zu bytes\n",
                  log_size, ROTATION_THRESHOLD * 100, total, target);
    pruneOldestEntries(target);
  }
}

void Logger::pruneOldestEntries(size_t target_size) {
  // Read all lines from log file
  File f = SPIFFS.open(LOG_FILE_PATH, FILE_READ);
  if (!f) {
    Serial.println("[LOGGER] Failed to open log file for rotation");
    return;
  }

  std::vector<String> lines;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.length() > 0) {
      line += "\n";
      lines.push_back(line);
    }
  }
  f.close();

  // Calculate current total size
  size_t current_size = 0;
  for (const auto& line : lines) {
    current_size += line.length();
  }

  // Calculate how many lines to skip to reach target size
  int skip_count = 0;
  size_t skipped_size = 0;
  while (skipped_size < (current_size - target_size) && skip_count < (int)lines.size()) {
    skipped_size += lines[skip_count].length();
    skip_count++;
  }

  // Ensure we keep at least 1 line if possible
  if (skip_count >= (int)lines.size() && lines.size() > 0) {
    skip_count = lines.size() - 1;
  }

  // Rewrite file with remaining lines
  f = SPIFFS.open(LOG_FILE_PATH, FILE_WRITE);  // Overwrites
  if (!f) {
    Serial.println("[LOGGER] Failed to open log file for rewriting");
    return;
  }

  for (int i = skip_count; i < (int)lines.size(); i++) {
    f.print(lines[i]);
  }
  f.close();

  // Log rotation event
  Serial.printf("[LOGGER] Rotated log: pruned %d entries, freed %zu bytes\n", skip_count, skipped_size);
}

// ========================
// File Append (Thread-Safe)
// ========================

bool Logger::appendToLog(const String& entry) {
  // Take mutex with 1000ms timeout
  if (log_mutex != nullptr) {
    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
      // Mutex timeout - output to Serial only
      Serial.println("[LOGGER] Mutex timeout, skipping file write");
      return false;
    }
  }

  // Append to file
  File f = SPIFFS.open(LOG_FILE_PATH, FILE_APPEND);
  if (!f) {
    if (log_mutex != nullptr) {
      xSemaphoreGive(log_mutex);
    }
    Serial.println("[LOGGER] Failed to open log file for append");
    return false;
  }

  size_t written = f.print(entry);
  f.close();

  if (log_mutex != nullptr) {
    xSemaphoreGive(log_mutex);
  }

  return written > 0;
}

// ========================
// Query Methods
// ========================

const char* Logger::getLogPath() const {
  return LOG_FILE_PATH;
}

uint32_t Logger::getBootSeq() const {
  return boot_seq;
}

uint32_t Logger::getEntrySeq() const {
  return entry_seq;
}
