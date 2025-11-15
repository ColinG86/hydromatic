#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ========================
// Device Information Struct
// ========================

struct DeviceInfo {
  char id[32];
  char type[16];
  uint8_t pin;
  int state;
  char last_change[32];
  char changed_by[32];
};

// ========================
// DeviceManager Class
// ========================

class DeviceManager {
public:
  // ========================
  // Singleton Access
  // ========================

  static DeviceManager& getInstance();

  // ========================
  // Lifecycle Methods
  // ========================

  void begin(const char* configPath = "/device_config.json");

  // ========================
  // Public API
  // ========================

  bool setState(const char* device_id, int value, const char* changed_by);
  int getState(const char* device_id);
  DeviceInfo getDeviceInfo(const char* device_id);
  std::vector<DeviceInfo> getAllDevices();

private:
  // ========================
  // Singleton Constructor (Private)
  // ========================

  DeviceManager();
  ~DeviceManager() = default;
  DeviceManager(const DeviceManager&) = delete;
  DeviceManager& operator=(const DeviceManager&) = delete;

  // ========================
  // Constants
  // ========================

  static const char* CONFIG_PATH;

  // ========================
  // State
  // ========================

  std::vector<DeviceInfo> devices;
  SemaphoreHandle_t device_mutex;
  bool initialized;

  // ========================
  // Internal Methods
  // ========================

  void loadConfiguration();
  void saveConfiguration();
  void initializeDevice(const DeviceInfo& device);
};

#endif // DEVICE_MANAGER_H