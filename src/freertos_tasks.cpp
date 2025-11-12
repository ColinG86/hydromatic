#include "freertos_tasks.h"
#include "wifi_manager.h"
#include <Arduino.h>

// ========================
// Global Queue Handles
// ========================

QueueHandle_t wifiStatusQueue = NULL;

// ========================
// Global State Tracking
// ========================

static WiFiConnectionState lastWiFiState = DISCONNECTED;
static WiFiOperatingMode lastWiFiMode = WIFI_OP_MODE_STATION;

// ========================
// WiFi Management Task
// ========================

/**
 * WiFi Management Task
 * Runs WiFiManager state machine at regular intervals
 * Publishes WiFi status events to queue when state changes
 *
 * Runs every 50ms to keep WiFi state machine responsive
 */
void wifiTask(void* pvParameters) {
  // Get reference to WiFiManager (passed as parameter)
  WiFiManager* pWiFiManager = (WiFiManager*)pvParameters;

  Serial.println("[FreeRTOS] WiFi task started");

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(50);  // Run every 50ms

  while (1) {
    // Wait until it's time to run again (every 50ms)
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    // Run WiFiManager state machine (non-blocking)
    pWiFiManager->handle();

    // Check if WiFi state has changed
    WiFiConnectionState currentState = pWiFiManager->getConnectionState();
    WiFiOperatingMode currentMode = pWiFiManager->getMode();
    bool isConnected = pWiFiManager->isConnected();

    // Only send event if state or mode changed
    if (currentState != lastWiFiState || currentMode != lastWiFiMode) {
      WiFiStatusEvent event;
      event.state = currentState;
      event.mode = currentMode;
      event.is_connected = isConnected;
      event.timestamp = millis();

      // Copy current SSID if in station mode
      if (currentMode == WIFI_OP_MODE_STATION) {
        String ssid = pWiFiManager->getCurrentSSID();
        strncpy(event.ssid, ssid.c_str(), sizeof(event.ssid) - 1);
        event.ssid[sizeof(event.ssid) - 1] = '\0';
        event.rssi = pWiFiManager->getSignalStrength();
      } else {
        event.ssid[0] = '\0';
        event.rssi = 0;
      }

      // Send event to queue (non-blocking, with timeout)
      if (xQueueSend(wifiStatusQueue, &event, pdMS_TO_TICKS(10)) != pdTRUE) {
        Serial.println("[WiFiTask] WARNING: Queue full, WiFi status event dropped");
      }

      // Update tracked state
      lastWiFiState = currentState;
      lastWiFiMode = currentMode;

      // Log state change
      Serial.print("[WiFiTask] State changed to: ");
      Serial.print(pWiFiManager->getConnectionStateString());
      Serial.print(" (");
      Serial.print(isConnected ? "CONNECTED" : "DISCONNECTED");
      Serial.println(")");
    }
  }
}

// ========================
// Main Orchestration Task
// ========================

/**
 * Main Orchestration Task
 * Subscribes to WiFi status queue
 * Handles system orchestration and OTA updates
 * Replaces traditional Arduino loop() with event-driven architecture
 *
 * Runs every 100ms (can process WiFi events immediately when they arrive)
 */
void mainTask(void* pvParameters) {
  Serial.println("[FreeRTOS] Main task started");

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(100);  // Run every 100ms

  // Heartbeat tracking
  static unsigned long lastHeartbeat = 0;
  static unsigned long lastStatusPrint = 0;

  while (1) {
    // Wait until it's time to run again (every 100ms)
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    // Try to receive WiFi status events (non-blocking)
    WiFiStatusEvent wifiEvent;
    if (xQueueReceive(wifiStatusQueue, &wifiEvent, 0) == pdTRUE) {
      // Process WiFi status change
      Serial.print("[MainTask] Received WiFi event: ");
      Serial.print(wifiEvent.is_connected ? "CONNECTED" : "DISCONNECTED");
      Serial.print(" - ");

      if (wifiEvent.mode == WIFI_OP_MODE_STATION) {
        Serial.print("Station (");
        Serial.print(wifiEvent.ssid);
        Serial.print(", RSSI=");
        Serial.print(wifiEvent.rssi);
        Serial.println("dBm)");
      } else {
        Serial.println("AP mode");
      }

      // TODO: Handle WiFi status changes (trigger OTA checks, service discovery, etc.)
    }

    // Heartbeat: print every 1 second to show device is alive
    unsigned long now = millis();
    if (now - lastHeartbeat >= 1000) {
      lastHeartbeat = now;
      Serial.print(".");
      Serial.flush();
    }

    // Print status every 10 seconds
    if (now - lastStatusPrint >= 10000) {
      lastStatusPrint = now;
      Serial.println();
      Serial.print("[MainTask] millis=");
      Serial.println(now);

      // TODO: Print system status and sensor readings
      // For now, just show task metrics
      Serial.print("[MainTask] Free heap: ");
      Serial.print(ESP.getFreeHeap());
      Serial.println(" bytes");
    }
  }
}

// ========================
// FreeRTOS Initialization
// ========================

/**
 * Initialize all FreeRTOS infrastructure
 * Creates task queues and starts all tasks
 * Called from setup() after WiFiManager is initialized
 */
void initializeFreeRTOS() {
  Serial.println("[FreeRTOS] Initializing FreeRTOS infrastructure...");

  // Create WiFi status queue (queue for 20 events)
  wifiStatusQueue = xQueueCreate(20, sizeof(WiFiStatusEvent));
  if (wifiStatusQueue == NULL) {
    Serial.println("[FreeRTOS] ERROR: Failed to create WiFi status queue!");
    return;
  }
  Serial.println("[FreeRTOS] WiFi status queue created");

  // Get pointer to global WiFiManager instance
  // We need to declare this extern in main.cpp
  extern WiFiManager wifiManager;

  // Create WiFi management task
  BaseType_t xReturned = xTaskCreate(
    wifiTask,                              // Task function
    "WiFiTask",                            // Task name
    TASK_STACK_WIFI / 4,                   // Stack size in words
    (void*)&wifiManager,                   // Parameter (WiFiManager pointer)
    TASK_PRIORITY_WIFI,                    // Priority
    NULL                                   // Task handle
  );
  if (xReturned != pdPASS) {
    Serial.println("[FreeRTOS] ERROR: Failed to create WiFi task!");
    return;
  }
  Serial.println("[FreeRTOS] WiFi task created");

  // Create main orchestration task
  xReturned = xTaskCreate(
    mainTask,                              // Task function
    "MainTask",                            // Task name
    TASK_STACK_MAIN / 4,                   // Stack size in words
    NULL,                                  // Parameter
    TASK_PRIORITY_MAIN,                    // Priority
    NULL                                   // Task handle
  );
  if (xReturned != pdPASS) {
    Serial.println("[FreeRTOS] ERROR: Failed to create main task!");
    return;
  }
  Serial.println("[FreeRTOS] Main task created");

  Serial.println("[FreeRTOS] All tasks created successfully");
  Serial.println("[FreeRTOS] Scheduler will now manage task execution");
}
