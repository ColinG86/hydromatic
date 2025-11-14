#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

/**
 * OTAManager - Manages Over-The-Air firmware updates via ArduinoOTA
 *
 * Provides wireless firmware update capability when WiFi is connected.
 * Integrates with WiFiManager for connection status.
 * Logs events via Logger module.
 *
 * Usage:
 *   OTAManager otaManager;
 *   otaManager.begin("/data/config.json");
 *
 *   // In main loop:
 *   otaManager.handle();  // Call frequently (every 100ms)
 *
 *   // Check if update in progress:
 *   if (otaManager.isUpdating()) {
 *     // Skip heavy operations
 *   }
 */
class OTAManager {
public:
  /**
   * Constructor - Initializes default values
   */
  OTAManager();

  /**
   * Initialize OTA manager
   * Loads configuration from SPIFFS and sets up ArduinoOTA callbacks
   *
   * @param configPath Path to config.json file (default: "/data/config.json")
   */
  void begin(const char* configPath = "/data/config.json");

  /**
   * Handle OTA operations
   * Must be called frequently (every 100ms recommended)
   *
   * Checks WiFi connection status, starts OTA server when connected,
   * and processes OTA events via ArduinoOTA.handle()
   */
  void handle();

  /**
   * Check if OTA is enabled in configuration
   * @return true if OTA is enabled, false otherwise
   */
  bool isEnabled() const;

  /**
   * Check if OTA update is currently in progress
   * Other tasks should skip heavy operations during updates
   *
   * @return true if update is in progress, false otherwise
   */
  bool isUpdating() const;

  /**
   * Get current upload progress
   * @return Progress percentage (0-100)
   */
  uint8_t getProgress() const;

  /**
   * Get current OTA status
   * @return Status string: "idle", "starting", "updating", "complete", "error"
   */
  String getStatus() const;

private:
  // Configuration
  bool enabled;                  // OTA enabled flag from config
  uint16_t port;                 // OTA port (default 3232)
  char hostname_prefix[20];      // Hostname prefix (default "hydromatic")

  // State
  bool ota_started;              // True after ArduinoOTA.begin() called
  bool updating;                 // True during upload
  uint8_t progress;              // Upload progress 0-100%
  String status;                 // Current status string

  // Last progress log (to avoid spam)
  uint8_t last_logged_progress;

  /**
   * Load configuration from SPIFFS
   * Parses config.json and reads ota section
   *
   * @param configPath Path to config file
   */
  void loadConfig(const char* configPath);

  /**
   * Setup ArduinoOTA callbacks and configuration
   * Configures hostname, port, and all event callbacks
   */
  void setupOTA();

  /**
   * Log event to Logger and Serial
   * @param message Message to log
   */
  void logEvent(const char* message);

  /**
   * Log formatted event to Logger and Serial
   * Printf-style formatting
   *
   * @param format Printf format string
   * @param ... Variable arguments
   */
  void logEventF(const char* format, ...);
};

#endif // OTA_MANAGER_H
