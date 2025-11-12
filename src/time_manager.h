#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ========================
// Time Confidence States
// ========================
enum TimeConfidenceState {
  TIME_UNCONFIDENT = 0,    // Time not synchronized, using fallback
  TIME_CONFIDENT = 1       // Time synchronized via NTP, trustworthy
};

// ========================
// NTP Sync States
// ========================
enum NTPSyncState {
  NTP_IDLE = 0,           // Not attempting sync
  NTP_SYNCING = 1,        // Currently attempting NTP sync
  NTP_SUCCESS = 2,        // Last sync was successful
  NTP_FAILED = 3          // Last sync failed or timed out
};

/**
 * TimeManager - Manages time synchronization and timezone conversion on ESP32
 *
 * Features:
 * - NTP synchronization with 5-second timeout on boot/WiFi connect
 * - Time confidence tracking (CONFIDENT if synced, UNCONFIDENT if fallback)
 * - POSIX TZ string support for timezone conversion
 * - Graceful offline operation (fallback to compile-time)
 * - FreeRTOS-aware: Uses mutexes for thread-safe access
 * - Non-blocking: All operations complete within timeout, don't block tasks
 * - Event logging with circular buffer
 *
 * Usage:
 *   TimeManager tm;
 *   tm.begin("/config.json");    // Load config, set compile-time as fallback
 *   // In FreeRTOS task:
 *   tm.handle();                 // Call periodically, handles NTP if WiFi available
 *   time_t t = tm.getTime();     // Get UTC time (thread-safe)
 *   time_t local = tm.getLocalTime();  // Get local time (with timezone applied)
 *   bool confident = tm.isTimeConfident();
 */
class TimeManager {
public:
  TimeManager();

  // ========================
  // Lifecycle Methods
  // ========================

  /**
   * Initialize TimeManager and load configuration
   * Sets compile-time as fallback, loads NTP server and timezone from config
   * @param configPath Path to config.json file (default: "/config.json" in SPIFFS)
   */
  void begin(const char* configPath = "/config.json");

  /**
   * Handle time synchronization state machine
   * Call this regularly in FreeRTOS task (e.g., every 1000ms from mainTask)
   * - Attempts NTP sync if WiFi is available
   * - Handles timeouts and state transitions
   * - Updates confidence state
   *
   * @param wifiIsConnected Pass true if WiFi is currently connected
   */
  void handle(bool wifiIsConnected);

  // ========================
  // Time Query Methods (Thread-Safe)
  // ========================

  /**
   * Get current UTC time
   * Thread-safe: protected by mutex
   * @return time_t (seconds since epoch)
   */
  time_t getTime();

  /**
   * Get current local time with timezone applied
   * Thread-safe: protected by mutex
   * Applies POSIX TZ string conversion
   * @return time_t in local timezone (seconds since epoch equivalent)
   */
  time_t getLocalTime();

  /**
   * Get time as broken-down structure (UTC)
   * Thread-safe: protected by mutex
   * @param[out] timeinfo Pointer to struct tm to fill
   * @return pointer to timeinfo, or nullptr on error
   */
  struct tm* getTimeInfo(struct tm* timeinfo);

  /**
   * Get time as broken-down structure (local timezone)
   * Thread-safe: protected by mutex
   * @param[out] timeinfo Pointer to struct tm to fill
   * @return pointer to timeinfo, or nullptr on error
   */
  struct tm* getLocalTimeInfo(struct tm* timeinfo);

  /**
   * Get time as formatted string
   * Thread-safe: protected by mutex
   * @param buffer Output buffer
   * @param size Buffer size
   * @param format strftime format string (e.g., "%Y-%m-%d %H:%M:%S")
   * @param useLocal true for local time, false for UTC
   * @return number of characters written (not including null terminator)
   */
  size_t getTimeString(char* buffer, size_t size, const char* format = "%Y-%m-%d %H:%M:%S", bool useLocal = true);

  // ========================
  // Confidence State Methods
  // ========================

  /**
   * Check if current time is confident (NTP synchronized)
   * Thread-safe: protected by mutex
   * @return true if synchronized via NTP, false if using fallback
   */
  bool isTimeConfident();

  /**
   * Get time confidence state enum
   * Thread-safe: protected by mutex
   * @return TIME_CONFIDENT or TIME_UNCONFIDENT
   */
  TimeConfidenceState getConfidenceState();

  /**
   * Get timestamp of last successful NTP sync
   * Thread-safe: protected by mutex
   * @return time_t of last sync, 0 if never synced
   */
  time_t getLastSyncTime();

  /**
   * Get milliseconds since last successful NTP sync
   * Thread-safe: protected by mutex
   * @return milliseconds, UINT32_MAX if never synced
   */
  uint32_t getMillisSinceSyncTime();

  // ========================
  // Timezone Methods
  // ========================

  /**
   * Get current timezone string (POSIX TZ format)
   * @return POSIX TZ string (e.g., "UTC0", "EST5EDT,M3.2.0,M11.1.0")
   */
  const char* getTimezone();

  /**
   * Set timezone string (POSIX TZ format)
   * Applies immediately via setenv("TZ", tz)
   * @param tz POSIX TZ string
   */
  void setTimezone(const char* tz);

  // ========================
  // Logging Methods
  // ========================

  /**
   * Print current time and confidence status
   * Format: "Time: YYYY-MM-DD HH:MM:SS (Confident|Unconfident) LastSync: HH:MM:SS"
   */
  void printStatus();

  /**
   * Print entire time sync event log
   * Shows up to 100 timestamped events for debugging
   */
  void printTimeLog();

  /**
   * Get number of log entries currently stored
   * @return Number of entries (0-100)
   */
  int getLogEntryCount();

private:
  // ========================
  // Configuration
  // ========================

  char ntp_server[64];                  // NTP server hostname
  char timezone[64];                    // POSIX TZ string (e.g., "UTC0")
  uint16_t ntp_timeout_seconds;         // NTP sync timeout in seconds
  uint32_t confidence_window_hours;     // Hours of sync confidence validity

  // ========================
  // Time State
  // ========================

  time_t compile_time;                  // Compile-time fallback (set at init)
  time_t current_time;                  // Current UTC time (protected by mutex)
  time_t last_sync_time;                // Last successful NTP sync time (protected by mutex)
  TimeConfidenceState confidence_state; // Current confidence (protected by mutex)

  // ========================
  // NTP Sync State Machine
  // ========================

  NTPSyncState ntp_state;               // Current NTP sync state
  unsigned long ntp_attempt_time;       // Timestamp when NTP attempt started
  uint32_t ntp_attempt_counter;         // Number of NTP attempts (for statistics)

  // ========================
  // Thread Safety (FreeRTOS)
  // ========================

  SemaphoreHandle_t time_mutex;         // Mutex protecting: current_time, last_sync_time, confidence_state

  // ========================
  // Logging
  // ========================

  static const int MAX_LOG_ENTRIES = 100;

  struct LogEntry {
    unsigned long timestamp;            // Milliseconds since boot
    char message[128];                  // Log message
  };

  LogEntry time_log[MAX_LOG_ENTRIES];   // Circular buffer
  int log_index;                        // Current write position

  // ========================
  // Internal Methods
  // ========================

  /**
   * Load time configuration from config.json
   * @param configPath Path to configuration file
   */
  void loadConfig(const char* configPath);

  /**
   * Set system time from epoch
   * Updates current_time and triggers logging
   * @param newTime time_t to set (UTC)
   */
  void setSystemTime(time_t newTime);

  /**
   * Perform NTP synchronization attempt
   * Non-blocking: uses configTime() with timeout
   * Updates NTP state, confidence state, and logging
   * Should only be called when WiFi is connected
   */
  void performNTPSync();

  /**
   * Update NTP state machine
   * Handles timeouts, retries, state transitions
   */
  void updateNTPState();

  /**
   * Calculate timezone offset from POSIX TZ string
   * @return seconds offset from UTC (negative for west, positive for east)
   */
  int calculateTimezoneOffset();

  /**
   * Log an event to the circular buffer
   * @param message Event message to log
   */
  void logEvent(const char* message);

  /**
   * Log an event with printf-style formatting
   * @param format Format string (printf-style)
   * @param ... Arguments for format string
   */
  void logEventF(const char* format, ...);

  /**
   * Get string representation of TimeConfidenceState
   * @param state State value
   * @return State name ("CONFIDENT" or "UNCONFIDENT")
   */
  static const char* getConfidenceString(TimeConfidenceState state);

  /**
   * Get string representation of NTPSyncState
   * @param state State value
   * @return State name (e.g., "SYNCING", "SUCCESS")
   */
  static const char* getNTPStateString(NTPSyncState state);
};

#endif // TIME_MANAGER_H
