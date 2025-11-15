#include "device_manager.h"
#include "logger.h"
#include <SPIFFS.h>

// ========================
// Constants
// ========================

const char* DeviceManager::CONFIG_PATH = "/device_config.json";

// ========================
// Singleton Access
// ========================

DeviceManager& DeviceManager::getInstance() {
  static DeviceManager instance;
  return instance;
}

// ========================
// Constructor (Private)
// ========================

DeviceManager::DeviceManager()
    : device_mutex(nullptr),
      initialized(false) {
}

// ========================
// Lifecycle Methods
// ========================

void DeviceManager::begin(const char* configPath) {
  // Use the provided configPath or the default CONFIG_PATH constant
  if (configPath != nullptr) {
    CONFIG_PATH = configPath;
  }
  
  if (device_mutex == nullptr) {
    device_mutex = xSemaphoreCreateMutex();
    if (device_mutex == nullptr) {
      Logger::getInstance().logError("DeviceManager: Failed to create mutex!");
      return;
    }
  }

  loadConfiguration();
  initialized = true;
  Logger::getInstance().logInfo("DeviceManager initialized with %d devices", devices.size());
}

// ========================
// Public API
// ========================

bool DeviceManager::setState(const char* device_id, int value, const char* changed_by) {
  if (!initialized) {
    Logger::getInstance().logError("DeviceManager not initialized.");
    return false;
  }

  if (xSemaphoreTake(device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    Logger::getInstance().logError("DeviceManager: Failed to take mutex for setState.");
    return false;
  }

  bool success = false;
  for (auto& device : devices) {
    if (strcmp(device.id, device_id) == 0) {
      if (strcmp(device.type, "digital_out") == 0) {
        digitalWrite(device.pin, value);
        device.state = value;
        strncpy(device.changed_by, changed_by, sizeof(device.changed_by) - 1);
        // TODO: Update last_change timestamp
        Logger::getInstance().logInfo("DeviceManager: Set digital_out device %s to state %d by %s", device.id, value, changed_by);
        success = true;
      } else if (strcmp(device.type, "adc") == 0) {
        // ADC is read-only, setState only updates cached value
        device.state = value;
        strncpy(device.changed_by, changed_by, sizeof(device.changed_by) - 1);
        // TODO: Update last_change timestamp
        Logger::getInstance().logInfo("DeviceManager: Updated ADC device %s cached state to %d by %s", device.id, value, changed_by);
        success = true;
      } else {
        Logger::getInstance().logWarning("DeviceManager: setState not supported for device type %s", device.type);
      }
      break;
    }
  }

  if (success) {
    saveConfiguration(); // Persist the change
  } else {
    Logger::getInstance().logWarning("DeviceManager: Device %s not found for setState operation.", device_id);
  }

  xSemaphoreGive(device_mutex);
  return success;
}

int DeviceManager::getState(const char* device_id) {
  if (!initialized) {
    Logger::getInstance().logError("DeviceManager not initialized.");
    return -1;
  }

  if (xSemaphoreTake(device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    Logger::getInstance().logError("DeviceManager: Failed to take mutex for getState.");
    return -1;
  }

  int state = -1;
  for (const auto& device : devices) {
    if (strcmp(device.id, device_id) == 0) {
      if (strcmp(device.type, "adc") == 0) {
        // For ADC, read the current analog value
        state = analogRead(device.pin);
        Logger::getInstance().logDebug("DeviceManager: Read ADC device %s, pin %d, value %d", device.id, device.pin, state);
      } else {
        state = device.state;
      }
      break;
    }
  }

  xSemaphoreGive(device_mutex);
  return state;
}

DeviceInfo DeviceManager::getDeviceInfo(const char* device_id) {
  if (!initialized) {
    Logger::getInstance().logError("DeviceManager not initialized.");
    DeviceInfo empty_device;
    memset(&empty_device, 0, sizeof(DeviceInfo));
    return empty_device;
  }

  if (xSemaphoreTake(device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    Logger::getInstance().logError("DeviceManager: Failed to take mutex for getDeviceInfo.");
    DeviceInfo empty_device;
    memset(&empty_device, 0, sizeof(DeviceInfo));
    return empty_device;
  }

  DeviceInfo info;
  memset(&info, 0, sizeof(DeviceInfo)); // Initialize to empty

  for (const auto& device : devices) {
    if (strcmp(device.id, device_id) == 0) {
      info = device;
      break;
    }
  }

  xSemaphoreGive(device_mutex);
  return info;
}

std::vector<DeviceInfo> DeviceManager::getAllDevices() {
  if (!initialized) {
    Logger::getInstance().logError("DeviceManager not initialized.");
    return {};
  }

  if (xSemaphoreTake(device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    Logger::getInstance().logError("DeviceManager: Failed to take mutex for getAllDevices.");
    return {};
  }

  std::vector<DeviceInfo> all_devices = devices; // Return a copy

  xSemaphoreGive(device_mutex);
  return all_devices;
}

// ========================
// Internal Methods
// ========================

void DeviceManager::loadConfiguration() {
  if (!SPIFFS.exists(CONFIG_PATH)) {
    Logger::getInstance().logWarning("DeviceManager: Config file not found at %s, using empty configuration.", CONFIG_PATH);
    return;
  }

  File configFile = SPIFFS.open(CONFIG_PATH, "r");
  if (!configFile) {
    Logger::getInstance().logError("DeviceManager: Failed to open config file at %s", CONFIG_PATH);
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Logger::getInstance().logError("DeviceManager: Config JSON parse error: %s", error.c_str());
    return;
  }

  if (!doc.containsKey("devices") || !doc["devices"].is<JsonArray>()) {
    Logger::getInstance().logError("DeviceManager: Config JSON missing 'devices' array.");
    return;
  }

  JsonArray devicesArray = doc["devices"].as<JsonArray>();
  devices.clear(); // Clear any existing devices

  for (JsonObject deviceObj : devicesArray) {
    DeviceInfo device;
    strncpy(device.id, deviceObj["id"] | "", sizeof(device.id) - 1);
    strncpy(device.type, deviceObj["type"] | "", sizeof(device.type) - 1);
    device.pin = deviceObj["pin"] | 0;
    device.state = deviceObj["state"] | 0;
    strncpy(device.last_change, deviceObj["last_change"] | "", sizeof(device.last_change) - 1);
    strncpy(device.changed_by, deviceObj["changed_by"] | "", sizeof(device.changed_by) - 1);

    devices.push_back(device);
    initializeDevice(device);
  }

  Logger::getInstance().logInfo("DeviceManager: Loaded %d devices from %s", devices.size(), CONFIG_PATH);
}

void DeviceManager::saveConfiguration() {
  if (device_mutex == nullptr) {
    Logger::getInstance().logError("DeviceManager: Mutex not initialized, cannot save configuration.");
    return;
  }

  if (xSemaphoreTake(device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    Logger::getInstance().logError("DeviceManager: Failed to take mutex for saving configuration.");
    return;
  }

  File configFile = SPIFFS.open(CONFIG_PATH, "w");
  if (!configFile) {
    Logger::getInstance().logError("DeviceManager: Failed to open config file for writing at %s", CONFIG_PATH);
    xSemaphoreGive(device_mutex);
    return;
  }

  JsonDocument doc;
  JsonArray devicesArray = doc.to<JsonArray>();

  for (const auto& device : devices) {
    JsonObject deviceObj = devicesArray.add<JsonObject>();
    deviceObj["id"] = device.id;
    deviceObj["type"] = device.type;
    deviceObj["pin"] = device.pin;
    deviceObj["state"] = device.state;
    deviceObj["last_change"] = device.last_change;
    deviceObj["changed_by"] = device.changed_by;
  }

  if (serializeJson(doc, configFile) == 0) {
    Logger::getInstance().logError("DeviceManager: Failed to write config JSON to %s", CONFIG_PATH);
  }
  configFile.close();
  xSemaphoreGive(device_mutex);
  Logger::getInstance().logInfo("DeviceManager: Saved %d devices to %s", devices.size(), CONFIG_PATH);
}

void DeviceManager::initializeDevice(const DeviceInfo& device) {
  if (strcmp(device.type, "digital_out") == 0) {
    pinMode(device.pin, OUTPUT);
    digitalWrite(device.pin, device.state);
    Logger::getInstance().logDebug("DeviceManager: Initialized digital_out device %s on pin %d to state %d", device.id, device.pin, device.state);
  } else if (strcmp(device.type, "adc") == 0) {
    // ADC pins don't need pinMode for input on ESP32, but we can log it.
    // analogReadResolution(12); // Example if needed
    Logger::getInstance().logDebug("DeviceManager: Initialized ADC device %s on pin %d", device.id, device.pin);
  } else {
    Logger::getInstance().logWarning("DeviceManager: Unknown device type %s for device %s", device.type, device.id);
  }
}
