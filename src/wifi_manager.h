#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <vector>
#include <cstring>

// WiFi credential structure
struct WiFiCredential {
  char ssid[33];      // ESP32 max SSID length is 32 + null terminator
  char password[64];  // ESP32 max password length is 63 + null terminator

  WiFiCredential() {
    memset(ssid, 0, sizeof(ssid));
    memset(password, 0, sizeof(password));
  }
};

// WiFi mode enum
enum WiFiOperatingMode {
  WIFI_OP_MODE_STATION = 0,
  WIFI_OP_MODE_AP = 1
};

// WiFi connection state enum
enum WiFiConnectionState {
  DISCONNECTED = 0,           // Initial state, no connection attempted
  CONNECTING_STATION = 1,     // Attempting to connect to current credential
  CONNECTED_STATION = 2,      // Successfully connected to WiFi
  DISCONNECTED_WAITING = 3,   // Waiting 60s after disconnection before trying next network
  RECONNECTING = 4,           // Retrying connection to same network
  STARTING_AP = 5,            // Initializing Access Point mode
  CONNECTED_AP = 6            // AP mode active (sticky state)
};

/**
 * WiFiManager - Manages WiFi connectivity for ESP32
 *
 * Features:
 * - Multiple WiFi credentials with priority ordering
 * - 5-attempt retry per network
 * - 60-second wait after disconnection before trying next network
 * - Automatic fallback to AP mode after all credentials exhausted
 * - Verbose WiFi event logging with circular buffer
 * - Non-blocking state machine in main loop via handle()
 */
class WiFiManager {
public:
  WiFiManager();

  // ========================
  // Core Lifecycle Methods
  // ========================

  /**
   * Initialize WiFiManager and load configuration
   * @param configPath Path to config.json file (default: "/config.json" in SPIFFS)
   */
  void begin(const char* configPath = "/config.json");

  /**
   * Handle WiFi state machine updates
   * Call this regularly in main loop (e.g., every 100ms)
   * Updates connection state, handles timeouts, transitions states
   */
  void handle();

  // ========================
  // Status Query Methods
  // ========================

  /**
   * Check if device is connected to WiFi (station or AP mode)
   * @return true if connected to station or AP mode active
   */
  bool isConnected();

  /**
   * Get current WiFi mode
   * @return WIFI_OP_MODE_STATION or WIFI_OP_MODE_AP
   */
  WiFiOperatingMode getMode();

  /**
   * Get local IP address as string
   * @return IP address (e.g., "192.168.1.100")
   */
  String getLocalIP();

  /**
   * Get device MAC address as string
   * @return MAC address (e.g., "AA:BB:CC:DD:EE:FF")
   */
  String getMACAddress();

  /**
   * Get WiFi signal strength (RSSI) in dBm
   * Only valid in station mode. Returns 0 in AP mode.
   * @return RSSI in dBm (typical range: -30 to -90)
   */
  int8_t getSignalStrength();

  // ========================
  // State Info Methods
  // ========================

  /**
   * Get current connection state
   * @return WiFiConnectionState enum value
   */
  WiFiConnectionState getConnectionState();

  /**
   * Get human-readable connection state string
   * @return State name (e.g., "CONNECTED_STATION", "CONNECTING_STATION")
   */
  String getConnectionStateString();

  /**
   * Get currently connected or connecting SSID
   * @return SSID string or empty if not connecting/connected
   */
  String getCurrentSSID();

  /**
   * Get index of current credential being used
   * @return 0-based index, -1 if no credential selected
   */
  int getCurrentCredentialIndex();

  /**
   * Get number of credentials loaded from config
   * @return Number of credentials
   */
  int getCredentialCount();

  /**
   * Get SSID of credential at given index
   * @param index 0-based credential index
   * @return SSID string or empty if index out of bounds
   */
  String getCredentialSSID(int index);

  // ========================
  // Logging Methods
  // ========================

  /**
   * Print current WiFi status (one-line summary)
   * Format: "WiFi: [MODE] SSID=... IP=... RSSI=..."
   */
  void printStatus();

  /**
   * Print entire WiFi event log
   * Shows up to 100 timestamped events for debugging
   */
  void printWiFiLog();

  /**
   * Get number of log entries currently stored
   * @return Number of entries (0-100)
   */
  int getLogEntryCount();

private:
  // ========================
  // Configuration
  // ========================

  std::vector<WiFiCredential> credentials;  // Ordered list of WiFi credentials
  char ap_password[64];                     // AP mode password
  char ap_ssid_prefix[20];                  // AP mode SSID prefix (e.g., "hydromatic")
  uint16_t connection_timeout_ms;           // Timeout for station connection attempt
  uint16_t reconnect_interval_ms;           // Interval between reconnect attempts
  uint32_t disconnection_threshold_ms;      // Time to wait after disconnection (60s)
  uint8_t max_attempts_per_network;         // Max attempts per credential (5)

  // ========================
  // State Tracking
  // ========================

  WiFiConnectionState state;                // Current connection state
  int current_credential_index;             // Index of credential being used (-1 if none)
  uint8_t attempt_counter;                  // Attempt count for current credential
  unsigned long state_change_time;          // Timestamp of last state change
  unsigned long disconnection_time;         // Timestamp when disconnection detected

  // ========================
  // Logging
  // ========================

  static const int MAX_LOG_ENTRIES = 100;   // Circular buffer size

  struct LogEntry {
    unsigned long timestamp;                // Milliseconds since boot
    char message[128];                      // Log message
  };

  LogEntry wifi_log[MAX_LOG_ENTRIES];       // Circular buffer
  int log_index;                            // Current write position in buffer

  // ========================
  // Internal State Methods
  // ========================

  /**
   * Load WiFi configuration from config.json
   * @param configPath Path to configuration file
   */
  void loadConfig(const char* configPath);

  /**
   * Attempt to connect to WiFi network
   * @param credential_index Index of credential to use
   */
  void connectToStation(int credential_index);

  /**
   * Initialize and activate AP mode
   */
  void startAPMode();

  /**
   * Update connection state based on WiFi.status()
   * Detects disconnections, successful connections, etc.
   */
  void updateConnectionState();

  /**
   * Handle state machine transitions
   * Main logic for moving between states, handling timeouts, etc.
   * Called from handle()
   */
  void handleStateTransitions();

  /**
   * Try to connect to next available credential
   * Resets attempt counter, updates state
   * Falls back to AP mode if no more credentials
   */
  void tryNextCredential();

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
   * Get string representation of WiFiOperatingMode enum
   * @param mode WiFiOperatingMode value
   * @return Mode name ("STATION" or "AP")
   */
  String getModeString(WiFiOperatingMode mode);

  /**
   * Get string representation of WiFiConnectionState enum
   * @param state WiFiConnectionState value
   * @return State name (e.g., "CONNECTING_STATION")
   */
  String getStateString(WiFiConnectionState state);
};

#endif // WIFI_MANAGER_H
