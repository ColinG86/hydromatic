#include "ota_manager.h"
#include "logger.h"
#include "wifi_manager.h"

OTAManager::OTAManager()
  : enabled(false),
    port(3232),
    ota_started(false),
    updating(false),
    progress(0),
    status("idle"),
    last_logged_progress(0) {
  strncpy(hostname_prefix, "hydromatic", sizeof(hostname_prefix) - 1);
  hostname_prefix[sizeof(hostname_prefix) - 1] = '\0';
}

void OTAManager::begin(const char* configPath) {
  Serial.println("[OTA] Initializing OTA Manager...");

  // Load configuration
  loadConfig(configPath);

  if (!enabled) {
    Serial.println("[OTA] OTA is disabled in configuration");
    return;
  }

  // Setup ArduinoOTA callbacks and configuration
  setupOTA();

  Serial.println("[OTA] OTA Manager initialized");
}

void OTAManager::loadConfig(const char* configPath) {
  File file = SPIFFS.open(configPath, "r");
  if (!file) {
    Serial.printf("[OTA] Failed to open config file: %s\n", configPath);
    return;
  }

  // Parse JSON
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.printf("[OTA] Failed to parse config: %s\n", error.c_str());
    return;
  }

  // Read OTA configuration
  JsonObject ota = doc["ota"];
  if (ota.isNull()) {
    Serial.println("[OTA] No 'ota' section in config");
    return;
  }

  enabled = ota["enabled"] | false;
  port = ota["port"] | 3232;

  const char* prefix = ota["hostname_prefix"];
  if (prefix) {
    strncpy(hostname_prefix, prefix, sizeof(hostname_prefix) - 1);
    hostname_prefix[sizeof(hostname_prefix) - 1] = '\0';
  }

  Serial.printf("[OTA] Config loaded - enabled: %d, port: %d, prefix: %s\n",
                enabled, port, hostname_prefix);
}

void OTAManager::setupOTA() {
  // Generate hostname with MAC address suffix
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char hostname[32];
  snprintf(hostname, sizeof(hostname), "%s-%02X%02X%02X",
           hostname_prefix, mac[3], mac[4], mac[5]);

  // Configure ArduinoOTA
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.setPort(port);

  Serial.printf("[OTA] Hostname: %s, Port: %d\n", hostname, port);

  // onStart callback - called when update begins
  ArduinoOTA.onStart([this]() {
    updating = true;
    status = "updating";
    progress = 0;
    last_logged_progress = 0;

    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";

    // Log to Logger module
    Logger::getInstance().logInfo("OTA update started: %s", type.c_str());

    // Log to Serial
    Serial.printf("[OTA] Update started: %s\n", type.c_str());
  });

  // onProgress callback - called during upload
  ArduinoOTA.onProgress([this](unsigned int current, unsigned int total) {
    progress = (current * 100) / total;

    // Log every 10% to avoid spam
    if (progress >= last_logged_progress + 10) {
      Serial.printf("[OTA] Progress: %u%%\n", progress);
      last_logged_progress = progress;
    }
  });

  // onEnd callback - called when update completes successfully
  ArduinoOTA.onEnd([this]() {
    Serial.println("\n[OTA] Update complete - REBOOTING");

    // Log to Logger module
    Logger::getInstance().logInfo("OTA update complete - REBOOTING");

    // Update status
    status = "complete";
    updating = false;

    // Flush serial buffer before reboot
    Serial.flush();
    delay(100);

    // Hard reboot
    ESP.restart();
  });

  // onError callback - called if update fails
  ArduinoOTA.onError([this](ota_error_t error) {
    const char* err;
    switch(error) {
      case OTA_AUTH_ERROR:
        err = "Auth Failed";
        break;
      case OTA_BEGIN_ERROR:
        err = "Begin Failed";
        break;
      case OTA_CONNECT_ERROR:
        err = "Connect Failed";
        break;
      case OTA_RECEIVE_ERROR:
        err = "Receive Failed";
        break;
      case OTA_END_ERROR:
        err = "End Failed";
        break;
      default:
        err = "Unknown Error";
        break;
    }

    // Log to Logger module
    Logger::getInstance().logError("OTA Error: %s", err);

    // Log to Serial
    Serial.printf("[OTA] Error: %s\n", err);

    // Update state
    updating = false;
    status = "error";
  });

  Serial.println("[OTA] Callbacks configured");
}

void OTAManager::handle() {
  if (!enabled) {
    return;
  }

  // Check WiFi connection status
  extern WiFiManager wifiManager;
  bool wifi_connected = wifiManager.isConnected();

  // Start OTA server when WiFi becomes available
  if (wifi_connected && !ota_started) {
    ArduinoOTA.begin();
    ota_started = true;

    logEvent("OTA server started");
  }

  // Handle OTA events (safe to call even if WiFi disconnected)
  if (ota_started) {
    ArduinoOTA.handle();
  }
}

bool OTAManager::isEnabled() const {
  return enabled;
}

bool OTAManager::isUpdating() const {
  return updating;
}

uint8_t OTAManager::getProgress() const {
  return progress;
}

String OTAManager::getStatus() const {
  return status;
}

void OTAManager::logEvent(const char* message) {
  Logger::getInstance().logInfo("%s", message);
  Serial.printf("[OTA] %s\n", message);
}

void OTAManager::logEventF(const char* format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  logEvent(buffer);
}
