#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * Logger - Singleton logging system with SPIFFS persistence and boot sequencing
 *
 * Features:
 * - JSON-line format logging to /data/active.log
 * - Boot sequence tracking with persistent counter
 * - Per-boot entry sequence numbering
 * - System stats attached to every entry (heap, PSRAM, SPIFFS, uptime, tasks)
 * - NO timestamps in file (calculated on send by NetworkLogger based on NTP sync)
 * - Circular buffer behavior: rotates at 80% SPIFFS capacity
 * - Thread-safe: mutex-protected file access
 * - Serial output for boot debugging
 * - Message truncation at 512 bytes with error logging
 *
 * Architecture:
 * - Singleton pattern for global access
 * - FreeRTOS mutex protects active.log file operations
 * - Boot counter persisted in /data/boot_counter.json
 * - Entry sequence counter maintained in RAM per boot
 *
 * JSON Format (append-only, one entry per line):
 * {"boot_seq":3,"uptime_ms":45000,"seq":42,"level":"info","msg":"...",
 *  "system":{"heap_free":234567,"heap_used":123456,"uptime_ms":45000,
 *            "free_psram":4194304,"task_count":5,"spiffs_free":987654,"spiffs_used":123456}}
 *
 * Usage:
 *   Logger::getInstance().begin();  // Call early in setup(), after SPIFFS.begin()
 *   Logger::getInstance().logInfo("WiFi connected to %s", ssid);
 *   Logger::getInstance().logError("Failed to mount: %d", error_code);
 */
class Logger {
public:
  // ========================
  // Singleton Access
  // ========================

  /**
   * Get singleton instance
   * @return Reference to the global Logger instance
   */
  static Logger& getInstance();

  // ========================
  // Lifecycle Methods
  // ========================

  /**
   * Initialize Logger subsystem
   * - Creates /data/ directory if missing
   * - Reads/initializes boot_counter.json (increments boot_seq on each boot)
   * - Resets entry sequence counter to 0
   * - Creates FreeRTOS mutex for file access
   * - Logs startup message
   *
   * MUST be called after SPIFFS.begin() but BEFORE other modules initialize
   * so they can log during their init.
   */
  void begin();

  // ========================
  // Logging Methods
  // ========================

  /**
   * Log DEBUG level message (verbose details for troubleshooting)
   * Format string with printf-style arguments
   * Appends to active.log with boot_seq, uptime_ms, seq, system stats
   * Outputs to Serial for boot debugging
   *
   * @param fmt Format string (printf-style)
   * @param ... Arguments for format string
   */
  void logDebug(const char* fmt, ...);

  /**
   * Log INFO level message (normal operational events)
   * Format string with printf-style arguments
   * Appends to active.log with boot_seq, uptime_ms, seq, system stats
   * Outputs to Serial for boot debugging
   *
   * @param fmt Format string (printf-style)
   * @param ... Arguments for format string
   */
  void logInfo(const char* fmt, ...);

  /**
   * Log WARNING level message (unexpected but recoverable events)
   * Format string with printf-style arguments
   * Appends to active.log with boot_seq, uptime_ms, seq, system stats
   * Outputs to Serial for boot debugging
   *
   * @param fmt Format string (printf-style)
   * @param ... Arguments for format string
   */
  void logWarning(const char* fmt, ...);

  /**
   * Log ERROR level message (errors requiring attention)
   * Format string with printf-style arguments
   * Appends to active.log with boot_seq, uptime_ms, seq, system stats
   * Outputs to Serial for boot debugging
   *
   * @param fmt Format string (printf-style)
   * @param ... Arguments for format string
   */
  void logError(const char* fmt, ...);

  // ========================
  // Query Methods
  // ========================

  /**
   * Get path to active log file
   * @return Path to active.log ("/data/active.log")
   */
  const char* getLogPath() const;

  /**
   * Get current boot sequence number
   * Increments on each boot, persisted in boot_counter.json
   * @return Boot sequence number (1-based)
   */
  uint32_t getBootSeq() const;

  /**
   * Get current entry sequence number
   * Increments with each log call within this boot
   * Resets to 0 on each boot
   * @return Entry sequence number (0-based)
   */
  uint32_t getEntrySeq() const;

  /**
   * Gather current system statistics
   * Called internally on every log entry
   * Can be called externally for monitoring
   *
   * Also triggers capacity check: if active.log > 80% SPIFFS, prunes oldest entries
   *
   * @param[out] doc ArduinoJson document to populate with system stats
   */
  void getSystemStats(JsonDocument& doc);

  /**
   * Get log file mutex handle
   * Used by NetworkLogger to synchronize access to active.log
   * @return SemaphoreHandle_t for log file mutex
   */
  SemaphoreHandle_t getLogMutex() const;

  /**
   * Delete first entry from active.log
   * Thread-safe: protected by log_mutex
   * Used by NetworkLogger after successful send and ack
   * Reads all lines, skips first, rewrites file
   * @return true if deleted successfully, false on error or empty file
   */
  bool deleteFirstEntry();

private:
  // ========================
  // Singleton Constructor (Private)
  // ========================

  Logger();
  ~Logger() = default;
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  // ========================
  // Constants
  // ========================

  static const char* LOG_FILE_PATH;       // "/data/active.log"
  static const char* BOOT_COUNTER_PATH;   // "/data/boot_counter.json"
  static const char* DATA_DIR_PATH;       // "/data"
  static const size_t MAX_MESSAGE_SIZE = 512;   // Message truncation limit
  static const float ROTATION_THRESHOLD;  // 0.8 (80% of SPIFFS)

  // ========================
  // State
  // ========================

  uint32_t boot_seq;           // Boot sequence number (persistent)
  uint32_t entry_seq;          // Entry sequence number (per-boot)
  SemaphoreHandle_t log_mutex; // Protects active.log file access
  bool initialized;            // Tracks begin() status

  // ========================
  // Internal Methods
  // ========================

  /**
   * Core logging implementation
   * Called by logDebug/Info/Warning/Error
   * - Formats message with vsnprintf
   * - Truncates if > MAX_MESSAGE_SIZE
   * - Gathers system stats
   * - Checks capacity and rotates if needed
   * - Appends JSON-line to active.log
   * - Outputs to Serial
   * - Increments entry_seq
   *
   * @param level Log level string ("debug", "info", "warning", "error")
   * @param fmt Format string
   * @param args va_list of format arguments
   */
  void log(const char* level, const char* fmt, va_list args);

  /**
   * Read boot counter from /data/boot_counter.json
   * Initializes to 0 if file missing or invalid
   * @return Boot sequence number
   */
  uint32_t readBootCounter();

  /**
   * Write boot counter to /data/boot_counter.json
   * Format: {"boot_seq": N}
   * @param seq Boot sequence number to write
   */
  void writeBootCounter(uint32_t seq);

  /**
   * Check if active.log exceeds rotation threshold (80% of SPIFFS)
   * If yes, prune oldest entries to bring it under threshold
   * Called during getSystemStats() on every log entry
   */
  void checkAndRotateLog();

  /**
   * Prune oldest entries from active.log
   * Strategy: Read all lines, skip N oldest, rewrite remaining
   * @param target_size Target size to reduce to (in bytes)
   */
  void pruneOldestEntries(size_t target_size);

  /**
   * Append JSON-line entry to active.log
   * Thread-safe: protected by log_mutex
   * @param entry JSON-line string (must end with \n)
   * @return true if written successfully, false on error
   */
  bool appendToLog(const String& entry);

  /**
   * Get SPIFFS total and used space
   * @param[out] total Total SPIFFS capacity in bytes
   * @param[out] used Used SPIFFS space in bytes
   */
  void getSPIFFSInfo(size_t& total, size_t& used);

  /**
   * Create /data/ directory if it doesn't exist
   */
  void ensureDataDirectory();
};

#endif // LOGGER_H
