#include "wifi_manager.h"
#include <SPIFFS.h>
#include <cstdio>
#include <cstdarg>

// ========================
// Constructor
// ========================

WiFiManager::WiFiManager()
    : state(DISCONNECTED),
      current_credential_index(-1),
      attempt_counter(0),
      state_change_time(0),
      disconnection_time(0),
      log_index(0) {
  // Initialize config defaults
  connection_timeout_ms = 10000;      // 10 seconds
  reconnect_interval_ms = 5000;       // 5 seconds
  disconnection_threshold_ms = 60000; // 60 seconds
  max_attempts_per_network = 5;

  memset(ap_password, 0, sizeof(ap_password));
  memset(ap_ssid_prefix, 0, sizeof(ap_ssid_prefix));
  strncpy(ap_ssid_prefix, "hydromatic", sizeof(ap_ssid_prefix) - 1);

  // Initialize log buffer
  for (int i = 0; i < MAX_LOG_ENTRIES; i++) {
    memset(wifi_log[i].message, 0, sizeof(wifi_log[i].message));
    wifi_log[i].timestamp = 0;
  }
}

// ========================
// Public Lifecycle Methods
// ========================

void WiFiManager::begin(const char* configPath) {
  logEvent("WiFiManager::begin() called");

  // Load configuration from file
  loadConfig(configPath);

  // Check if we have any credentials
  if (credentials.empty()) {
    logEvent("ERROR: No WiFi credentials loaded from config");
    state = STARTING_AP;
    startAPMode();
    return;
  }

  logEventF("Loaded %d WiFi credential(s)", credentials.size());

  // Set hostname VERY EARLY before WiFi mode change - required for DHCP hostname registration
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char hostname[33];
  memset(hostname, 0, sizeof(hostname));
  snprintf(hostname, sizeof(hostname), "%s-%02X%02X%02X", ap_ssid_prefix, mac[3], mac[4], mac[5]);
  WiFi.setHostname(hostname);
  logEventF("Hostname set early: %s", hostname);
  delay(100);  // Give hostname time to register

  // Initialize WiFi in station mode and start connecting to first credential
  WiFi.mode(WIFI_STA);
  state = CONNECTING_STATION;
  current_credential_index = 0;
  attempt_counter = 0;
  state_change_time = millis();

  connectToStation(0);
}

void WiFiManager::handle() {
  // Update connection state based on WiFi.status()
  updateConnectionState();

  // Handle state machine transitions
  handleStateTransitions();
}

// ========================
// Private Config Loading
// ========================

void WiFiManager::loadConfig(const char* configPath) {
  logEventF("Loading config from %s", configPath);

  // Try to read config file
  File configFile = SPIFFS.open(configPath, "r");
  if (!configFile) {
    logEventF("ERROR: Could not open %s", configPath);
    return;
  }

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    logEventF("ERROR: JSON parse failed: %s", error.c_str());
    return;
  }

  // Extract WiFi config section
  JsonObject wifiConfig = doc["wifi"];
  if (!wifiConfig) {
    logEvent("ERROR: No 'wifi' section in config");
    return;
  }

  // Load connection parameters
  if (wifiConfig["connection_timeout_ms"]) {
    connection_timeout_ms = wifiConfig["connection_timeout_ms"];
  }
  if (wifiConfig["reconnect_interval_ms"]) {
    reconnect_interval_ms = wifiConfig["reconnect_interval_ms"];
  }
  if (wifiConfig["disconnection_threshold_ms"]) {
    disconnection_threshold_ms = wifiConfig["disconnection_threshold_ms"];
  }
  if (wifiConfig["max_attempts_per_network"]) {
    max_attempts_per_network = wifiConfig["max_attempts_per_network"];
  }

  // Load credentials array
  JsonArray credArray = wifiConfig["credentials"];
  if (!credArray) {
    logEvent("ERROR: No 'credentials' array in config");
    return;
  }

  for (JsonObject cred : credArray) {
    if (!cred["ssid"] || !cred["password"]) {
      logEvent("WARNING: Skipping credential with missing ssid or password");
      continue;
    }

    WiFiCredential newCred;
    strncpy(newCred.ssid, cred["ssid"], sizeof(newCred.ssid) - 1);
    strncpy(newCred.password, cred["password"], sizeof(newCred.password) - 1);

    credentials.push_back(newCred);
    logEventF("  - Credential %d: %s", credentials.size(), newCred.ssid);
  }

  // Load AP configuration
  JsonObject apConfig = wifiConfig["ap"];
  if (apConfig) {
    if (apConfig["password"]) {
      strncpy(ap_password, apConfig["password"], sizeof(ap_password) - 1);
    }
    if (apConfig["ssid_prefix"]) {
      strncpy(ap_ssid_prefix, apConfig["ssid_prefix"], sizeof(ap_ssid_prefix) - 1);
    }
  }

  logEventF("Config loaded: timeout=%ums, reconnect=%ums, threshold=%ums, max_attempts=%d",
            connection_timeout_ms, reconnect_interval_ms, disconnection_threshold_ms,
            max_attempts_per_network);
}

// ========================
// Private Connection Methods
// ========================

void WiFiManager::connectToStation(int credential_index) {
  if (credential_index < 0 || credential_index >= (int)credentials.size()) {
    logEvent("ERROR: Invalid credential index");
    return;
  }

  current_credential_index = credential_index;
  attempt_counter = 0;
  state = CONNECTING_STATION;
  state_change_time = millis();

  WiFiCredential& cred = credentials[credential_index];

  logEventF("Connecting to \"%s\" (attempt %d/%d)", cred.ssid, attempt_counter + 1,
            max_attempts_per_network);

  // Set hostname BEFORE connecting so DHCP server gets it during handshake
  // Only do this on first attempt to avoid issues
  if (attempt_counter == 0) {
    uint8_t mac[6];
    WiFi.macAddress(mac);  // Gets MAC, returns void
    char hostname[33];
    memset(hostname, 0, sizeof(hostname));
    snprintf(hostname, sizeof(hostname), "%s-%02X%02X%02X", ap_ssid_prefix, mac[3], mac[4], mac[5]);
    WiFi.setHostname(hostname);
    logEventF("Hostname set to: %s", hostname);
  }

  // Disconnect any existing connection first
  WiFi.disconnect(false); // false = do not turn off WiFi radio

  // Connect to WiFi network
  WiFi.begin(cred.ssid, cred.password);
}

void WiFiManager::startAPMode() {
  logEvent("Activating AP mode");
  state = STARTING_AP;
  state_change_time = millis();

  // Get MAC address for SSID suffix
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssid[33];
  snprintf(ssid, sizeof(ssid), "%s-%02X%02X%02X", ap_ssid_prefix, mac[3], mac[4], mac[5]);

  logEventF("AP SSID: \"%s\"", ssid);

  // Stop station mode if active
  WiFi.mode(WIFI_AP);

  // Start AP mode
  WiFi.softAP(ssid, ap_password);

  // Set static IP (192.168.4.1 is default for AP mode)
  // Enable DNS spoofing - all DNS queries resolve to AP IP
  // This is handled by DNSServer in web config interface (future)

  state = CONNECTED_AP;
  state_change_time = millis();
  logEventF("AP mode active: SSID=\"%s\", IP=192.168.4.1", ssid);
}

// ========================
// Private State Management
// ========================

void WiFiManager::updateConnectionState() {
  wl_status_t status = WiFi.status();

  // If we're in a station-related state, check actual WiFi status
  if (state == CONNECTING_STATION || state == CONNECTED_STATION || state == RECONNECTING) {
    if (status == WL_CONNECTED) {
      // Successfully connected (only log if transitioning into connected state)
      if (state != CONNECTED_STATION) {
        state = CONNECTED_STATION;
        state_change_time = millis();
        logEventF("Connected to \"%s\" (IP: %s, RSSI: %d dBm)", getCurrentSSID().c_str(),
                  getLocalIP().c_str(), getSignalStrength());
        attempt_counter = 0;

        // Get MAC address for mDNS hostname
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char hostname[33];
        snprintf(hostname, sizeof(hostname), "%s-%02X%02X%02X", ap_ssid_prefix, mac[3], mac[4], mac[5]);

        // Initialize mDNS for hostname resolution
        // Note: MDNS.end() may be needed if reconnecting to prevent conflicts
        MDNS.end();  // Clear any previous mDNS instances

        if (!MDNS.begin(hostname)) {
          logEvent("ERROR: mDNS initialization failed");
        } else {
          // Add service advertisements to ensure hostname is advertised
          MDNS.addService("http", "tcp", 80);
          logEventF("mDNS initialized - hostname: %s.local", hostname);
        }
      }
      // Already connected, don't log repeatedly
    } else if (status == WL_DISCONNECTED && state == CONNECTED_STATION) {
      // Was connected, now disconnected
      state = DISCONNECTED_WAITING;
      state_change_time = millis();
      disconnection_time = millis();
      logEventF("Disconnected from \"%s\"", getCurrentSSID().c_str());
      logEventF("Waiting %lu ms before trying next network...", disconnection_threshold_ms);
    }
  }
}

void WiFiManager::handleStateTransitions() {
  unsigned long now = millis();

  switch (state) {
    case DISCONNECTED:
      // Initial state, wait for begin() to be called
      break;

    case CONNECTING_STATION: {
      // Check if connection attempt has timed out
      unsigned long elapsed = now - state_change_time;

      if (WiFi.status() == WL_CONNECTED) {
        // Connection successful, updateConnectionState() will handle this
        break;
      }

      if (elapsed >= connection_timeout_ms) {
        // Timeout occurred
        attempt_counter++;
        logEventF("Connection attempt %d/%d failed (timeout)", attempt_counter,
                  max_attempts_per_network);

        if (attempt_counter >= max_attempts_per_network) {
          // Exhausted attempts for this credential
          tryNextCredential();
        } else {
          // Retry same credential
          logEventF("Retrying in %u ms...", reconnect_interval_ms);
          state_change_time = now;
          WiFi.disconnect(false);
          WiFi.begin(credentials[current_credential_index].ssid,
                     credentials[current_credential_index].password);
        }
      }
      break;
    }

    case CONNECTED_STATION:
      // Normal connected state, updateConnectionState() detects disconnections
      break;

    case DISCONNECTED_WAITING: {
      // Waiting 60 seconds after disconnection before trying next network
      unsigned long elapsed = now - disconnection_time;

      if (elapsed >= disconnection_threshold_ms) {
        // Timeout elapsed, try next credential
        logEvent("60s timeout elapsed. Trying next credential...");
        tryNextCredential();
      }
      break;
    }

    case RECONNECTING: {
      // Attempting to reconnect to same network
      unsigned long elapsed = now - state_change_time;

      if (WiFi.status() == WL_CONNECTED) {
        // Connection successful, updateConnectionState() will handle this
        break;
      }

      if (elapsed >= connection_timeout_ms) {
        // Timeout occurred
        attempt_counter++;
        logEventF("Reconnection attempt %d/%d failed", attempt_counter,
                  max_attempts_per_network);

        if (attempt_counter >= max_attempts_per_network) {
          // Exhausted attempts for this credential
          tryNextCredential();
        } else {
          // Retry same credential
          logEventF("Retrying in %u ms...", reconnect_interval_ms);
          state_change_time = now;
          WiFi.disconnect(false);
          WiFi.begin(credentials[current_credential_index].ssid,
                     credentials[current_credential_index].password);
        }
      }
      break;
    }

    case STARTING_AP:
      // startAPMode() sets state to CONNECTED_AP
      break;

    case CONNECTED_AP:
      // AP mode active, sticky state (no auto fallback)
      break;

    default:
      logEventF("WARNING: Unknown state %d", state);
      break;
  }
}

void WiFiManager::tryNextCredential() {
  current_credential_index++;

  if (current_credential_index >= (int)credentials.size()) {
    // No more credentials to try, start AP mode
    logEvent("All credentials exhausted. Starting AP mode...");
    startAPMode();
  } else {
    // Try next credential
    logEventF("Trying next credential: \"%s\"", credentials[current_credential_index].ssid);
    connectToStation(current_credential_index);
  }
}

// ========================
// Public Status Methods
// ========================

bool WiFiManager::isConnected() {
  return (state == CONNECTED_STATION || state == CONNECTED_AP);
}

WiFiOperatingMode WiFiManager::getMode() {
  if (state == CONNECTED_AP || state == STARTING_AP) {
    return WIFI_OP_MODE_AP;
  }
  return WIFI_OP_MODE_STATION;
}

String WiFiManager::getLocalIP() {
  if (state == CONNECTED_STATION) {
    return WiFi.localIP().toString();
  } else if (state == CONNECTED_AP) {
    return WiFi.softAPIP().toString();
  }
  return "";
}

String WiFiManager::getMACAddress() {
  return WiFi.macAddress();
}

int8_t WiFiManager::getSignalStrength() {
  if (state == CONNECTED_STATION) {
    return WiFi.RSSI();
  }
  return 0;
}

WiFiConnectionState WiFiManager::getConnectionState() { return state; }

String WiFiManager::getConnectionStateString() { return getStateString(state); }

String WiFiManager::getCurrentSSID() {
  if (current_credential_index >= 0 && current_credential_index < (int)credentials.size()) {
    return String(credentials[current_credential_index].ssid);
  }
  return "";
}

int WiFiManager::getCurrentCredentialIndex() { return current_credential_index; }

int WiFiManager::getCredentialCount() { return credentials.size(); }

String WiFiManager::getCredentialSSID(int index) {
  if (index >= 0 && index < (int)credentials.size()) {
    return String(credentials[index].ssid);
  }
  return "";
}

// ========================
// Public Logging Methods
// ========================

void WiFiManager::printStatus() {
  Serial.print("WiFi: [");
  Serial.print(getModeString(getMode()));
  Serial.print("] ");

  if (isConnected()) {
    Serial.print("Connected ");
    if (getMode() == WIFI_OP_MODE_STATION) {
      Serial.print("to \"");
      Serial.print(getCurrentSSID());
      Serial.print("\" ");
    }
    Serial.print("IP=");
    Serial.print(getLocalIP());

    if (getMode() == WIFI_OP_MODE_STATION) {
      Serial.print(" RSSI=");
      Serial.print(getSignalStrength());
      Serial.print("dBm");
    }
  } else {
    Serial.print("Disconnected State=");
    Serial.print(getConnectionStateString());
  }

  Serial.println();
}

void WiFiManager::printWiFiLog() {
  Serial.println("\n======== WiFi Log ========");
  Serial.println("(Most recent last)");
  Serial.println();

  int count = 0;
  for (int i = 0; i < MAX_LOG_ENTRIES; i++) {
    int idx = (log_index + i) % MAX_LOG_ENTRIES;
    if (wifi_log[idx].timestamp == 0) {
      // Entry not yet used
      continue;
    }

    Serial.print("[");
    Serial.print(wifi_log[idx].timestamp);
    Serial.print("] ");
    Serial.println(wifi_log[idx].message);
    count++;
  }

  if (count == 0) {
    Serial.println("(No log entries)");
  }

  Serial.println("========================\n");
}

int WiFiManager::getLogEntryCount() {
  int count = 0;
  for (int i = 0; i < MAX_LOG_ENTRIES; i++) {
    if (wifi_log[i].timestamp != 0) {
      count++;
    }
  }
  return count;
}

// ========================
// Private Logging Methods
// ========================

void WiFiManager::logEvent(const char* message) {
  unsigned long now = millis();

  // Write to circular buffer
  wifi_log[log_index].timestamp = now;
  strncpy(wifi_log[log_index].message, message, sizeof(wifi_log[log_index].message) - 1);
  wifi_log[log_index].message[sizeof(wifi_log[log_index].message) - 1] = '\0';

  // Move to next position (wraps around)
  log_index = (log_index + 1) % MAX_LOG_ENTRIES;

  // Output to Serial for real-time debugging during development
  Serial.print("[WiFi ");
  Serial.print(now);
  Serial.print("] ");
  Serial.println(message);
}

void WiFiManager::logEventF(const char* format, ...) {
  char buffer[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  logEvent(buffer);
}

// ========================
// Private Helper Methods
// ========================

String WiFiManager::getModeString(WiFiOperatingMode mode) {
  switch (mode) {
    case WIFI_OP_MODE_STATION:
      return "STATION";
    case WIFI_OP_MODE_AP:
      return "AP";
    default:
      return "UNKNOWN";
  }
}

String WiFiManager::getStateString(WiFiConnectionState state) {
  switch (state) {
    case DISCONNECTED:
      return "DISCONNECTED";
    case CONNECTING_STATION:
      return "CONNECTING_STATION";
    case CONNECTED_STATION:
      return "CONNECTED_STATION";
    case DISCONNECTED_WAITING:
      return "DISCONNECTED_WAITING";
    case RECONNECTING:
      return "RECONNECTING";
    case STARTING_AP:
      return "STARTING_AP";
    case CONNECTED_AP:
      return "CONNECTED_AP";
    default:
      return "UNKNOWN";
  }
}
