#include <Arduino.h>
#include <SPIFFS.h>
#include "logger.h"
#include "wifi_manager.h"
#include "time_manager.h"
#include "network_logger.h"
#include "ota_manager.h"
#include "freertos_tasks.h"
#include "device_manager.h" // Include DeviceManager header
#include "cycle_manager.h" // Include CycleManager header

// ========================
// Global Manager Instances
// ========================
WiFiManager wifiManager;
TimeManager timeManager;
NetworkLogger networkLogger;
OTAManager otaManager;
// DeviceManager deviceManager; // Global instance of DeviceManager - REMOVED

// ========================
// Setup - Initialize Hardware and FreeRTOS
// ========================

void setup() {
  // Initialize serial for debugging - EARLY OUTPUT
  Serial.begin(115200);
  delay(200);

  Serial.println("\n\n[BOOT] Hydromatic Device Starting");

  // Initialize SPIFFS filesystem
  Serial.println("[SETUP] Mounting SPIFFS...");
  if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] SPIFFS mount failed!");
    return;
  }
  Serial.println("[SETUP] SPIFFS mounted successfully");

  // Initialize Logger subsystem (MUST be early so other modules can log during init)
  Logger::getInstance().begin();

  // Initialize WiFi Manager with config from SPIFFS
  Serial.println("[SETUP] Initializing WiFiManager...");
  wifiManager.begin("/config.json");
  Serial.println("[SETUP] WiFiManager initialized");

  // Initialize Time Manager with config from SPIFFS
  Serial.println("[SETUP] Initializing TimeManager...");
  timeManager.begin("/config.json");
  Serial.println("[SETUP] TimeManager initialized");

  // Initialize OTA Manager with config from SPIFFS
  Serial.println("[SETUP] Initializing OTAManager...");
  otaManager.begin("/config.json");
  Serial.println("[SETUP] OTAManager initialized");

  // Initialize Device Manager with config from SPIFFS
  Serial.println("[SETUP] Initializing DeviceManager...");
  DeviceManager::getInstance().begin("/device_config.json"); // Get instance and call begin
  Serial.println("[SETUP] DeviceManager initialized");

  // Initialize Cycle Manager
  Serial.println("[SETUP] Initializing CycleManager...");
  CycleManager::getInstance()->setup("/config.json");
  Serial.println("[SETUP] CycleManager initialized");

  // Initialize FreeRTOS infrastructure (creates tasks and queues)
  // NOTE: NetworkLogger.begin() is called inside initializeFreeRTOS()
  // because it needs the networkCommandQueue to be created first
  Serial.println("[SETUP] Initializing FreeRTOS tasks...");
  initializeFreeRTOS();

  Serial.println("[SETUP] System ready - FreeRTOS scheduler running");

  // Log final startup message
  Logger::getInstance().logInfo("Hydromatic system initialization complete");
}

// ========================
// Loop - Managed by FreeRTOS
// ========================

/**
 * Arduino loop() - Now Managed by FreeRTOS
 * All actual work is done in FreeRTOS tasks
 * This just yields to the scheduler
 */
void loop() {
  vTaskDelay(pdMS_TO_TICKS(100));
}
