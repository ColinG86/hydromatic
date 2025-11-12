#include <Arduino.h>
#include <SPIFFS.h>
#include "logger.h"
#include "wifi_manager.h"
#include "time_manager.h"
#include "freertos_tasks.h"

// ========================
// Global Manager Instances
// ========================
WiFiManager wifiManager;
TimeManager timeManager;

// ========================
// Setup - Initialize Hardware and FreeRTOS
// ========================

void setup() {
  // Initialize serial for debugging - EARLY OUTPUT
  delay(100);
  Serial.begin(115200);
  delay(100);

  // Heartbeat: output immediately to show device is alive
  Serial.println("\n\n[BOOT] Device booting...");
  Serial.flush();
  delay(500);

  Serial.println("\n\n");
  Serial.println("=== Hydromatic System Starting (FreeRTOS Multi-Tasking) ===");

  // Initialize SPIFFS filesystem
  Serial.println("[SETUP] Initializing SPIFFS...");
  Serial.flush();
  if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] SPIFFS initialization failed!");
    Serial.flush();
    return;
  }
  Serial.println("[SETUP] SPIFFS initialized successfully.");
  Serial.flush();

  // Initialize Logger subsystem (MUST be early so other modules can log during init)
  Logger::getInstance().begin();
  Serial.flush();

  // Initialize WiFi Manager with config from SPIFFS
  Serial.println("[SETUP] Initializing WiFiManager...");
  Serial.flush();
  wifiManager.begin("/config.json");
  Serial.println("[SETUP] WiFiManager initialized successfully.");
  Serial.flush();

  // Initialize Time Manager with config from SPIFFS
  Serial.println("[SETUP] Initializing TimeManager...");
  Serial.flush();
  timeManager.begin("/config.json");
  Serial.println("[SETUP] TimeManager initialized successfully.");
  Serial.flush();

  // Initialize FreeRTOS infrastructure (creates tasks and queues)
  Serial.println("[SETUP] Initializing FreeRTOS multi-tasking...");
  Serial.flush();
  initializeFreeRTOS();

  Serial.println("[SETUP] Setup complete - FreeRTOS scheduler starting");
  Serial.println("[SETUP] All tasks will now be managed by FreeRTOS");
  Serial.flush();

  // Log final startup message
  Logger::getInstance().logInfo("Hydromatic system initialization complete, FreeRTOS starting");

  // Note: Arduino's loop() function is no longer used after setup()
  // FreeRTOS scheduler takes over and manages all task execution
  // The setup task will be cleaned up automatically
}

// ========================
// Loop - No Longer Used
// ========================

/**
 * Arduino loop() - Now Managed by FreeRTOS
 *
 * IMPORTANT: The Arduino framework still calls this function, but we yield to FreeRTOS
 * All actual work is done in FreeRTOS tasks:
 * - wifiTask() - Handles WiFi state machine
 * - mainTask() - Handles system orchestration
 *
 * This loop() function now acts as a low-priority yield point that allows
 * FreeRTOS to manage all task scheduling and execution.
 */
void loop() {
  // Yield control to FreeRTOS scheduler
  // This is the idle task - just yield repeatedly
  vTaskDelay(pdMS_TO_TICKS(100));
}
