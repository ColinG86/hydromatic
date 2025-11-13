#include "network_logger.h"
#include "freertos_tasks.h"
#include <SPIFFS.h>
#include <WiFi.h>
#include <time.h>

// ========================
// Constructor
// ========================

NetworkLogger::NetworkLogger()
    : tcp_server_port(5000),
      ack_timeout_ms(2000),
      heartbeat_interval_ms(1000),
      is_connected(false),
      last_send_time(0),
      retry_index(0),
      next_retry_time(0),
      time_manager(nullptr),
      command_queue(nullptr),
      log_mutex(nullptr) {
  // Initialize buffers
  memset(tcp_server_host, 0, sizeof(tcp_server_host));
  memset(retry_backoff_ms, 0, sizeof(retry_backoff_ms));

  // Initialize pointer
  tcp_client = nullptr;

  // Default values
  strncpy(tcp_server_host, "work-laptop.local", sizeof(tcp_server_host) - 1);
  retry_backoff_ms[0] = 5000;
  retry_backoff_ms[1] = 10000;
  retry_backoff_ms[2] = 30000;
}

// ========================
// Lifecycle Methods
// ========================

void NetworkLogger::begin(const char* configPath, TimeManager* timeManager, QueueHandle_t cmdQueue) {
  Serial.println("[NETLOG] Initializing NetworkLogger...");

  // Store references
  time_manager = timeManager;
  command_queue = cmdQueue;

  // Get Logger's mutex for active.log synchronization
  log_mutex = Logger::getInstance().getLogMutex();
  if (log_mutex == nullptr) {
    Serial.println("[NETLOG] WARNING: Logger mutex is null! File operations may not be thread-safe.");
  }

  // Dynamically allocate the WiFiClient
  tcp_client = new WiFiClient();

  // Load configuration
  loadConfig(configPath);

  Serial.printf("[NETLOG] Config: server=%s:%d, ack_timeout=%dms, heartbeat=%dms\n",
                tcp_server_host, tcp_server_port, ack_timeout_ms, heartbeat_interval_ms);
  Serial.printf("[NETLOG] Backoff: [%d, %d, %d] ms\n",
                retry_backoff_ms[0], retry_backoff_ms[1], retry_backoff_ms[2]);

  Serial.println("[NETLOG] NetworkLogger initialized - ready for task loop");
}

void NetworkLogger::handle() {
  // Check for incoming commands (non-blocking)
  if (is_connected) {
    checkForCommands();
  }

  // Check if we're in backoff period
  if (next_retry_time > 0 && millis() < next_retry_time) {
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }

  // Ensure TCP connection
  if (!is_connected) {
    is_connected = connectToServer();
    if (!is_connected) {
      applyBackoff();
      return;
    }
    resetBackoff();
    last_send_time = millis(); // Initialize heartbeat timer
  }

  // Try to read first entry from active.log
  String entry;
  if (readFirstEntry(entry)) {
    // Parse entry to extract boot_seq and uptime_ms
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, entry);

    if (error) {
      // Corrupted entry - handle and continue
      Serial.printf("[NETLOG] Corrupted JSON entry: %s\n", error.c_str());
      handleCorruptedEntry(entry);

      // Delete corrupted entry so we can move on
      if (Logger::getInstance().deleteFirstEntry()) {
        Serial.println("[NETLOG] Deleted corrupted entry");
      }
      return;
    }

    // Extract fields
    uint32_t boot_seq = doc["boot_seq"];
    uint32_t entry_uptime_ms = doc["uptime_ms"];

    // Calculate timestamp
    String timestamp;
    bool has_timestamp = calculateTimestamp(boot_seq, entry_uptime_ms, timestamp);
    if (!has_timestamp) {
      timestamp = "null";
    }

    // Send entry with calculated timestamp
    if (sendEntry(entry, timestamp)) {
      // Wait for ack
      if (waitForAck()) {
        // Success! Delete entry from active.log
        if (Logger::getInstance().deleteFirstEntry()) {
          // Entry deleted, update last_send_time
          last_send_time = millis();
          resetBackoff();
        } else {
          Serial.println("[NETLOG] WARNING: Failed to delete entry after ack");
        }
      } else {
        // No ack received - retry with backoff
        Serial.println("[NETLOG] No ack received, will retry");
        applyBackoff();

        // Disconnect to force reconnection
        tcp_client->stop();
        is_connected = false;
      }
    } else {
      // Send failed - retry with backoff
      Serial.println("[NETLOG] Send failed, will retry");
      applyBackoff();

      // Disconnect
      tcp_client->stop();
      is_connected = false;
    }
  } else {
    // No entries to send - check if should send heartbeat
    if (shouldSendHeartbeat()) {
      if (sendHeartbeat()) {
        last_send_time = millis();
        resetBackoff();
      } else {
        Serial.println("[NETLOG-D] handle: heartbeat FAILED, disconnecting");
        // Heartbeat failed - apply backoff
        applyBackoff();
        tcp_client->stop();
        is_connected = false;
      }
    } else {
      // Nothing to do - small delay
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

// ========================
// Configuration
// ========================

void NetworkLogger::loadConfig(const char* configPath) {
  if (!SPIFFS.exists(configPath)) {
    Serial.println("[NETLOG] Config file not found - using defaults");
    return;
  }

  File configFile = SPIFFS.open(configPath, "r");
  if (!configFile) {
    Serial.println("[NETLOG] Failed to open config file");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Serial.printf("[NETLOG] Config JSON parse error: %s\n", error.c_str());
    return;
  }

  // Load tcp_logging configuration section
  if (doc.containsKey("tcp_logging")) {
    JsonObject tcpConfig = doc["tcp_logging"];

    if (tcpConfig.containsKey("server_host")) {
      strncpy(tcp_server_host, tcpConfig["server_host"], sizeof(tcp_server_host) - 1);
    }

    if (tcpConfig.containsKey("server_port")) {
      tcp_server_port = tcpConfig["server_port"];
    }

    if (tcpConfig.containsKey("ack_timeout_ms")) {
      ack_timeout_ms = tcpConfig["ack_timeout_ms"];
    }

    if (tcpConfig.containsKey("heartbeat_interval_ms")) {
      heartbeat_interval_ms = tcpConfig["heartbeat_interval_ms"];
    }

    if (tcpConfig.containsKey("retry_backoff_ms")) {
      JsonArray backoff = tcpConfig["retry_backoff_ms"];
      for (size_t i = 0; i < 3 && i < backoff.size(); i++) {
        retry_backoff_ms[i] = backoff[i];
      }
    }
  }

  Serial.println("[NETLOG] Configuration loaded successfully");
}

// ========================
// TCP Connection
// ========================

bool NetworkLogger::connectToServer() {
  Serial.printf("[NETLOG-D] connectToServer: checking WiFi status (is %d)\n", WiFi.status());
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NETLOG-D] connectToServer: WiFi not connected, aborting.");
    return false;
  }

  // CRITICAL FIX: Stop any existing connection before attempting new one
  if (tcp_client->connected()) {
    Serial.println("[NETLOG-D] connectToServer: stopping existing client.");
    tcp_client->stop();
  }

  Serial.printf("[NETLOG] Connecting to %s:%d...\n", tcp_server_host, tcp_server_port);

  // Attempt connection with 5 second timeout
  tcp_client->setTimeout(5);

  Serial.println("[NETLOG-D] connectToServer: calling tcp_client->connect()...");
  bool connect_result = tcp_client->connect(tcp_server_host, tcp_server_port);
  Serial.printf("[NETLOG-D] connectToServer: tcp_client->connect() returned %d\n", connect_result);

  if (!connect_result) {
    // Connection failed - log with WiFi status for debugging
    Serial.printf("[NETLOG] Connection FAILED (WiFi status: %d)\n", WiFi.status());
    return false;
  }

  Serial.printf("[NETLOG] Connected to TCP server. client.connected()=%d\n", tcp_client->connected());
  return true;
}

// ========================
// Log File Reading
// ========================

bool NetworkLogger::readFirstEntry(String& entry) {
  entry = "";

  // Take mutex
  if (log_mutex != nullptr) {
    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
      Serial.println("[NETLOG] readFirstEntry: Mutex timeout");
      return false;
    }
  }

  const char* log_path = Logger::getInstance().getLogPath();

  // Check if file exists and is not empty
  if (!SPIFFS.exists(log_path)) {
    if (log_mutex != nullptr) {
      xSemaphoreGive(log_mutex);
    }
    return false;
  }

  File f = SPIFFS.open(log_path, FILE_READ);
  if (!f || f.size() == 0) {
    if (f) {
      f.close();
    }
    if (log_mutex != nullptr) {
      xSemaphoreGive(log_mutex);
    }
    return false;
  }

  // Read first non-empty line
  bool found = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.length() > 0) {
      entry = line;
      found = true;
      break;
    }
  }

  f.close();

  if (log_mutex != nullptr) {
    xSemaphoreGive(log_mutex);
  }

  return found;
}

// ========================
// Timestamp Calculation
// ========================

bool NetworkLogger::calculateTimestamp(uint32_t boot_seq, uint32_t entry_uptime_ms, String& timestamp) {
  if (time_manager == nullptr) {
    timestamp = "null";
    return false;
  }

  // Look up NTP sync info for this boot_seq
  time_t ntp_sync_time = 0;
  uint32_t sync_uptime_ms = 0;

  if (!time_manager->getNTPHistoryForBoot(boot_seq, ntp_sync_time, sync_uptime_ms)) {
    // Boot sequence not found or never synced NTP
    timestamp = "null";
    return false;
  }

  // Calculate timestamp: ntp_sync_time - sync_uptime_ms + entry_uptime_ms
  // Convert to seconds: ntp_sync_time + (entry_uptime_ms - sync_uptime_ms) / 1000
  int32_t uptime_delta_ms = entry_uptime_ms - sync_uptime_ms;
  time_t entry_time = ntp_sync_time + (uptime_delta_ms / 1000);

  // Format as ISO 8601: YYYY-MM-DDTHH:MM:SSZ
  struct tm timeinfo;
  gmtime_r(&entry_time, &timeinfo);

  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           timeinfo.tm_year + 1900,
           timeinfo.tm_mon + 1,
           timeinfo.tm_mday,
           timeinfo.tm_hour,
           timeinfo.tm_min,
           timeinfo.tm_sec);

  timestamp = String(buf);
  return true;
}

// ========================
// Sending
// ========================

bool NetworkLogger::sendEntry(const String& entry, const String& timestamp) {
  if (!is_connected) {
    return false;
  }

  // Parse entry JSON
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, entry);
  if (error) {
    return false;
  }

  // Add timestamp field
  if (timestamp == "null") {
    doc["ts"] = nullptr;
  } else {
    doc["ts"] = timestamp;
  }

  // Serialize and send
  String json_line;
  serializeJson(doc, json_line);
  json_line += "\n";

  size_t written = tcp_client->print(json_line);
  return (written == json_line.length());
}

bool NetworkLogger::waitForAck() {
  if (!is_connected) {
    return false;
  }

  // Set socket timeout
  tcp_client->setTimeout(ack_timeout_ms);

  // Wait for data
  unsigned long start = millis();
  while (millis() - start < ack_timeout_ms) {
    if (tcp_client->available()) {
      // Read line
      String ack_line = tcp_client->readStringUntil('\n');

      // Parse JSON
      StaticJsonDocument<128> doc;
      DeserializationError error = deserializeJson(doc, ack_line);

      if (!error && doc.containsKey("ack") && doc["ack"] == 1) {
        // Valid ack received
        return true;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // Timeout
  return false;
}

bool NetworkLogger::sendHeartbeat() {
  Serial.println("[NETLOG-D] sendHeartbeat: called");

  if (!is_connected) {
    Serial.println("[NETLOG-D] sendHeartbeat: not connected, aborting");
    return false;
  }

  // Get current boot_seq and uptime
  uint32_t boot_seq = Logger::getInstance().getBootSeq();
  uint32_t uptime_ms = millis();

  // Calculate timestamp for current time
  String timestamp;
  bool has_timestamp = calculateTimestamp(boot_seq, uptime_ms, timestamp);
  if (!has_timestamp) {
    timestamp = "null";
  }

  // Gather system stats
  StaticJsonDocument<1024> stats_doc;
  Logger::getInstance().getSystemStats(stats_doc);

  // Build heartbeat JSON
  StaticJsonDocument<2048> heartbeat_doc;
  heartbeat_doc["boot_seq"] = boot_seq;
  heartbeat_doc["uptime_ms"] = uptime_ms;

  if (timestamp == "null") {
    heartbeat_doc["ts"] = nullptr;
  } else {
    heartbeat_doc["ts"] = timestamp;
  }

  heartbeat_doc["type"] = "heartbeat";
  heartbeat_doc["system"] = stats_doc;

  // Serialize and send
  String json_line;
  serializeJson(heartbeat_doc, json_line);
  json_line += "\n";

  Serial.printf("[NETLOG-D] sendHeartbeat: JSON size %d, calling print...\n", json_line.length());
  size_t written = tcp_client->print(json_line);
  Serial.printf("[NETLOG-D] sendHeartbeat: tcp_client->print() wrote %d bytes\n", written);

  if (written != json_line.length()) {
    Serial.println("[NETLOG-D] sendHeartbeat: print() FAILED");
    return false;
  }

  Serial.println("[NETLOG-D] sendHeartbeat: sent, waiting for ack...");
  bool ack_result = waitForAck();
  Serial.printf("[NETLOG-D] sendHeartbeat: waitForAck() returned %d\n", ack_result);
  return ack_result;
}

// ========================
// Command Handling
// ========================

void NetworkLogger::checkForCommands() {
  if (!tcp_client->available()) {
    return;
  }

  // Try to read a line (non-blocking)
  String cmd_line = tcp_client->readStringUntil('\n');
  if (cmd_line.length() == 0) {
    return;
  }

  // Parse command JSON
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, cmd_line);

  if (error || !doc.containsKey("cmd")) {
    // Invalid command, ignore
    return;
  }

  const char* cmd_type = doc["cmd"];
  Serial.printf("[NETLOG] Received command: %s\n", cmd_type);

  // Handle "status" command
  if (strcmp(cmd_type, "status") == 0) {
    // Generate status log entry immediately
    Logger::getInstance().logInfo("Status requested (command from server)");
    // Normal log flow will send this entry
  }

  // Queue command to application task (if queue provided)
  if (command_queue != nullptr) {
    NetworkCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    strncpy(cmd.type, cmd_type, sizeof(cmd.type) - 1);
    cmd.timestamp = millis();

    // Non-blocking queue send
    xQueueSend(command_queue, &cmd, 0);
  }
}

// ========================
// Error Handling
// ========================

void NetworkLogger::handleCorruptedEntry(const String& raw_entry) {
  if (!is_connected) {
    return;
  }

  // Build error entry
  uint32_t boot_seq = Logger::getInstance().getBootSeq();
  uint32_t uptime_ms = millis();

  // Calculate timestamp
  String timestamp;
  bool has_timestamp = calculateTimestamp(boot_seq, uptime_ms, timestamp);
  if (!has_timestamp) {
    timestamp = "null";
  }

  // Gather system stats
  StaticJsonDocument<1024> stats_doc;
  Logger::getInstance().getSystemStats(stats_doc);

  // Truncate raw entry for error message
  String truncated = raw_entry.substring(0, 100);
  if (raw_entry.length() > 100) {
    truncated += "...";
  }

  // Build error JSON
  StaticJsonDocument<2048> error_doc;
  error_doc["boot_seq"] = boot_seq;
  error_doc["uptime_ms"] = uptime_ms;

  if (timestamp == "null") {
    error_doc["ts"] = nullptr;
  } else {
    error_doc["ts"] = timestamp;
  }

  error_doc["level"] = "error";
  error_doc["msg"] = "Corrupted log entry detected and skipped: " + truncated;
  error_doc["system"] = stats_doc;

  // Serialize and send
  String json_line;
  serializeJson(error_doc, json_line);
  json_line += "\n";

  tcp_client->print(json_line);

  // Wait for ack (best effort, don't block on failure)
  waitForAck();
}

// ========================
// Backoff Management
// ========================

void NetworkLogger::applyBackoff() {
  // Get backoff delay for current retry_index
  uint16_t delay_ms;
  if (retry_index < 3) {
    delay_ms = retry_backoff_ms[retry_index];
    retry_index++;
  } else {
    // Stay at maximum backoff (30s)
    delay_ms = retry_backoff_ms[2];
  }

  next_retry_time = millis() + delay_ms;
  Serial.printf("[NETLOG-D] applyBackoff: next retry in %d ms\n", delay_ms);
}

void NetworkLogger::resetBackoff() {
  retry_index = 0;
  next_retry_time = 0;
  Serial.println("[NETLOG-D] resetBackoff: backoff reset");
}

bool NetworkLogger::shouldSendHeartbeat() {
  // If never sent anything, initialize last_send_time to allow first heartbeat
  if (last_send_time == 0) {
    last_send_time = millis();
  }

  unsigned long elapsed = millis() - last_send_time;
  bool should_send = (elapsed >= heartbeat_interval_ms);
  return should_send;
}
