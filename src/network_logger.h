#ifndef NETWORK_LOGGER_H
#define NETWORK_LOGGER_H

#include <Arduino.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include "logger.h"
#include "time_manager.h"

/**
 * NetworkLogger - FreeRTOS task for TCP log synchronization
 *
 * Features:
 * - Reads log entries from /data/active.log (mutex-protected)
 * - Calculates timestamps using NTP history (null if boot never synced)
 * - Sends JSON-line entries to TCP server
 * - Waits for per-entry acknowledgments
 * - Deletes confirmed entries from active.log
 * - Sends heartbeat every 1s of idle time
 * - Handles corrupted JSON (wraps error, sends to server, skips)
 * - Silent exponential backoff on failures (no logging of transient errors)
 * - Receives commands from server (queued to application task)
 * - Non-blocking design with timeouts
 * - Runs on core 0, independent from application logic
 *
 * Usage:
 *   NetworkLogger netLogger;
 *   netLogger.begin("/config.json", &timeManager, commandQueue);
 *   // In FreeRTOS:
 *   xTaskCreatePinnedToCore(networkLoggerTask, "NetLog", ..., (void*)&netLogger, ..., 0);
 */

// Forward declare NetworkCommand (defined in freertos_tasks.h)
struct NetworkCommand;

// ========================
// NetworkLogger Class
// ========================

class NetworkLogger {
public:
  NetworkLogger();

  // ========================
  // Lifecycle Methods
  // ========================

  /**
   * Initialize NetworkLogger and load configuration
   * @param configPath Path to config.json (default: "/config.json")
   * @param timeManager Pointer to TimeManager instance (for NTP history lookup)
   * @param cmdQueue Queue handle for passing commands to application task
   */
  void begin(const char* configPath, TimeManager* timeManager, QueueHandle_t cmdQueue);

  /**
   * Main task loop - called repeatedly by FreeRTOS task
   * Handles: connection, log reading, sending, acks, heartbeats, commands
   * Non-blocking with timeouts throughout
   */
  void handle();

private:
  // ========================
  // Configuration
  // ========================

  char tcp_server_host[64];       // TCP server hostname or IP
  uint16_t tcp_server_port;       // TCP server port
  uint16_t ack_timeout_ms;        // Timeout waiting for ack
  uint16_t heartbeat_interval_ms; // Idle time before sending heartbeat
  uint16_t retry_backoff_ms[3];   // Exponential backoff delays [5000, 10000, 30000]

  // ========================
  // State
  // ========================

  WiFiClient* tcp_client;          // Changed to pointer
  bool is_connected;              // Whether TCP is currently connected
  unsigned long last_send_time;   // Timestamp of last successful send or heartbeat
  uint8_t retry_index;            // Current index in retry_backoff_ms array
  unsigned long next_retry_time;  // When to retry after backoff

  TimeManager* time_manager;      // Pointer to TimeManager (for NTP history)
  QueueHandle_t command_queue;    // Queue to application task (for commands)
  SemaphoreHandle_t log_mutex;    // Logger's mutex (for active.log access)

  // ========================
  // Internal Methods
  // ========================

  /**
   * Load configuration from config.json
   * @param configPath Path to configuration file
   */
  void loadConfig(const char* configPath);

  /**
   * Attempt TCP connection to server
   * Non-blocking with timeout
   * @return true if connected, false otherwise
   */
  bool connectToServer();

  /**
   * Read first entry from active.log
   * Line-by-line reading, stops at first valid JSON
   * Mutex-protected
   * @param[out] entry String to fill with JSON entry
   * @return true if entry read, false if file empty or error
   */
  bool readFirstEntry(String& entry);

  /**
   * Calculate timestamp for log entry
   * Looks up boot_seq in NTP history, computes ISO 8601 timestamp
   * @param boot_seq Boot sequence from log entry
   * @param entry_uptime_ms Uptime when entry was logged
   * @param[out] timestamp ISO 8601 string (e.g., "2025-11-13T12:00:00Z") or "null"
   * @return true if timestamp calculated, false if null (boot never synced NTP)
   */
  bool calculateTimestamp(uint32_t boot_seq, uint32_t entry_uptime_ms, String& timestamp);

  /**
   * Send JSON-line entry to TCP server
   * Adds calculated timestamp to entry before sending
   * @param entry JSON entry string (without newline)
   * @param timestamp Calculated timestamp ("null" or ISO 8601)
   * @return true if sent successfully, false on error
   */
  bool sendEntry(const String& entry, const String& timestamp);

  /**
   * Wait for acknowledgment from server
   * Non-blocking with ack_timeout_ms timeout
   * Expected: {"ack":1}\n
   * @return true if ack received, false on timeout or error
   */
  bool waitForAck();

  /**
   * Generate and send heartbeat
   * Heartbeat format: {"boot_seq":N,"uptime_ms":M,"ts":null|"...","type":"heartbeat","system":{...}}
   * Does NOT write to active.log
   * @return true if sent and acked, false otherwise
   */
  bool sendHeartbeat();

  /**
   * Check for incoming commands from server (non-blocking)
   * If command received, parse and queue to application task
   * Supported commands: {"cmd":"status"} (initial implementation)
   */
  void checkForCommands();

  /**
   * Handle corrupted JSON entry
   * Wraps error message, sends to server, returns to continue processing
   * Format: {"boot_seq":X,"uptime_ms":Y,"level":"error","msg":"Corrupted entry: ...","system":{...}}
   * @param raw_entry Raw corrupted line
   */
  void handleCorruptedEntry(const String& raw_entry);

  /**
   * Apply exponential backoff
   * Updates retry_index and next_retry_time
   * Backoff progression: 5s, 10s, 30s, then steady 30s
   */
  void applyBackoff();

  /**
   * Reset backoff on successful operation
   */
  void resetBackoff();

  /**
   * Check if enough time has elapsed since last send for heartbeat
   * @return true if idle for >= heartbeat_interval_ms
   */
  bool shouldSendHeartbeat();
};

#endif // NETWORK_LOGGER_H
