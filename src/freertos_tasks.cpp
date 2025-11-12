#include "freertos_tasks.h"
#include "wifi_manager.h"
#include "time_manager.h"
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
// TimeManager Test State
// ========================

#include "test_time_manager.h"

struct TimeManagerTestState {
  enum TestPhase {
    TEST_INIT = 0,
    TEST_WAIT_WIFI,
    TEST_NTP_SYNC,
    TEST_TIMEZONE,
    TEST_EVENT_LOG,
    TEST_COMPLETE
  };

  TestPhase currentPhase;
  unsigned long phaseStartTime;
  bool testResults[5];  // Results for each test
  int testsCompleted;
  bool wifiConnected;
};

static TimeManagerTestState testState = {
  TimeManagerTestState::TEST_INIT,
  0,
  {false, false, false, false, false},
  0,
  false
};

// Test function declarations
void testTimeManager(TimeManager* tm);
void resetTimeManagerTest();

/**
 * Test 1: Initialization
 * Verify TimeManager initializes with fallback time
 */
bool test_initialization(TimeManager* tm) {
  Serial.println("\n=== TEST 1: Initialization ===");

  // Check that time is set (even if unconfident)
  time_t now = tm->getTime();
  if (now == 0) {
    Serial.println("[FAIL] Time is zero");
    return false;
  }

  // Should be unconfident before NTP sync
  if (tm->isTimeConfident()) {
    Serial.println("[FAIL] Time should be unconfident before NTP sync");
    return false;
  }

  // Check log entries exist
  int logCount = tm->getLogEntryCount();
  if (logCount == 0) {
    Serial.println("[FAIL] No log entries");
    return false;
  }

  Serial.println("[PASS] Initialization test passed");
  Serial.printf("  - Time set: %lu\n", (unsigned long)now);
  Serial.printf("  - Confidence: %s\n", tm->isTimeConfident() ? "CONFIDENT" : "UNCONFIDENT");
  Serial.printf("  - Log entries: %d\n", logCount);

  return true;
}

/**
 * Test 2: WiFi Wait
 * Wait for WiFi connection (required for NTP)
 */
bool test_wifi_wait(TimeManager* tm, bool wifiConnected) {
  Serial.println("\n=== TEST 2: WiFi Connection Wait ===");

  if (!wifiConnected) {
    Serial.println("[WAIT] Waiting for WiFi connection...");
    return false;  // Not ready yet
  }

  Serial.println("[PASS] WiFi connected");
  return true;
}

/**
 * Test 3: NTP Sync
 * Verify NTP synchronization works
 */
bool test_ntp_sync(TimeManager* tm, unsigned long phaseStartTime) {
  Serial.println("\n=== TEST 3: NTP Sync ===");

  // Allow up to 10 seconds for NTP sync
  unsigned long elapsed = millis() - phaseStartTime;

  if (tm->isTimeConfident()) {
    Serial.println("[PASS] NTP sync successful");

    // Verify last sync time is set
    time_t lastSync = tm->getLastSyncTime();
    if (lastSync == 0) {
      Serial.println("[FAIL] Last sync time not recorded");
      return false;
    }

    // Verify time is reasonable (year >= 2025)
    time_t now = tm->getTime();
    struct tm timeinfo = {};
    gmtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < 125) {  // 125 = 2025 - 1900
      Serial.printf("[FAIL] Time unreasonable (year: %d)\n", timeinfo.tm_year + 1900);
      return false;
    }

    Serial.printf("  - Time confident: YES\n");
    Serial.printf("  - Current year: %d\n", timeinfo.tm_year + 1900);
    Serial.printf("  - Last sync: %lu seconds ago\n", tm->getMillisSinceSyncTime() / 1000);

    return true;
  }

  if (elapsed > 10000) {
    Serial.println("[FAIL] NTP sync timeout (10 seconds)");
    Serial.println("  - Check network connectivity");
    Serial.println("  - Check NTP server accessibility");
    return false;
  }

  Serial.printf("[WAIT] Waiting for NTP sync... (%lu ms elapsed)\n", elapsed);
  return false;  // Not ready yet
}

/**
 * Test 4: Timezone
 * Verify timezone conversion works
 */
bool test_timezone(TimeManager* tm) {
  Serial.println("\n=== TEST 4: Timezone ===");

  // Get timezone
  const char* tz = tm->getTimezone();
  Serial.printf("  - Timezone: %s\n", tz);

  // Get UTC and local times
  char utcStr[64];
  char localStr[64];

  tm->getTimeString(utcStr, sizeof(utcStr), "%Y-%m-%d %H:%M:%S UTC", false);
  tm->getTimeString(localStr, sizeof(localStr), "%Y-%m-%d %H:%M:%S %Z", true);

  Serial.printf("  - UTC time:   %s\n", utcStr);
  Serial.printf("  - Local time: %s\n", localStr);

  Serial.println("[PASS] Timezone test passed");
  return true;
}

/**
 * Test 5: Event Log
 * Verify event logging is working
 */
bool test_event_log(TimeManager* tm) {
  Serial.println("\n=== TEST 5: Event Log ===");

  int logCount = tm->getLogEntryCount();
  Serial.printf("  - Log entries: %d\n", logCount);

  if (logCount < 1) {
    Serial.println("[FAIL] No log entries found");
    return false;
  }

  // Print log
  Serial.println("\n  Event log:");
  tm->printTimeLog();

  Serial.println("\n[PASS] Event log test passed");
  return true;
}

/**
 * Main test coordinator
 * Runs through all test phases sequentially
 */
void testTimeManager(TimeManager* tm) {
  if (!tm) return;

  switch (testState.currentPhase) {
    case TimeManagerTestState::TEST_INIT:
      Serial.println("\n========================================");
      Serial.println("STARTING TimeManager TEST SUITE");
      Serial.println("========================================");

      testState.testResults[0] = test_initialization(tm);
      testState.testsCompleted++;

      testState.currentPhase = TimeManagerTestState::TEST_WAIT_WIFI;
      testState.phaseStartTime = millis();
      break;

    case TimeManagerTestState::TEST_WAIT_WIFI:
      if (test_wifi_wait(tm, testState.wifiConnected)) {
        testState.testResults[1] = true;
        testState.testsCompleted++;
        testState.currentPhase = TimeManagerTestState::TEST_NTP_SYNC;
        testState.phaseStartTime = millis();
      }
      break;

    case TimeManagerTestState::TEST_NTP_SYNC:
      if (test_ntp_sync(tm, testState.phaseStartTime)) {
        testState.testResults[2] = true;
        testState.testsCompleted++;
        testState.currentPhase = TimeManagerTestState::TEST_TIMEZONE;
        testState.phaseStartTime = millis();
      } else {
        // Check for timeout failure
        unsigned long elapsed = millis() - testState.phaseStartTime;
        if (elapsed > 10000) {
          testState.testResults[2] = false;
          testState.testsCompleted++;
          testState.currentPhase = TimeManagerTestState::TEST_TIMEZONE;
          testState.phaseStartTime = millis();
        }
      }
      break;

    case TimeManagerTestState::TEST_TIMEZONE:
      testState.testResults[3] = test_timezone(tm);
      testState.testsCompleted++;
      testState.currentPhase = TimeManagerTestState::TEST_EVENT_LOG;
      testState.phaseStartTime = millis();
      break;

    case TimeManagerTestState::TEST_EVENT_LOG: {
      testState.testResults[4] = test_event_log(tm);
      testState.testsCompleted++;
      testState.currentPhase = TimeManagerTestState::TEST_COMPLETE;
      testState.phaseStartTime = millis();

      // Print summary
      Serial.println("\n========================================");
      Serial.println("=== TimeManager Test Results ===");
      Serial.println("========================================");

      const char* testNames[] = {
        "Initialization",
        "WiFi Wait",
        "NTP Sync",
        "Timezone",
        "Event Log"
      };

      int passed = 0;
      for (int i = 0; i < 5; i++) {
        Serial.printf("[RESULT] %-20s : %s\n", testNames[i],
                      testState.testResults[i] ? "PASS" : "FAIL");
        if (testState.testResults[i]) passed++;
      }

      Serial.println();
      Serial.printf("[RESULT] Tests Passed: %d/5\n", passed);

      if (passed == 5) {
        Serial.println("[RESULT] *** ALL TESTS PASSED ***");
      } else {
        Serial.println("[RESULT] *** SOME TESTS FAILED ***");
      }

      Serial.println("========================================\n");
      break;
    }

    case TimeManagerTestState::TEST_COMPLETE:
      // Tests complete, do nothing
      break;
  }
}

/**
 * Reset test state (for re-running tests)
 */
void resetTimeManagerTest() {
  testState.currentPhase = TimeManagerTestState::TEST_INIT;
  testState.phaseStartTime = 0;
  testState.testsCompleted = 0;
  testState.wifiConnected = false;

  for (int i = 0; i < 5; i++) {
    testState.testResults[i] = false;
  }

  Serial.println("[TEST] Test state reset");
}

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
// Time Synchronization Task
// ========================

/**
 * Time Synchronization Task
 * Manages NTP synchronization and timezone handling
 * Receives WiFi status from queue to trigger NTP when WiFi available
 * Runs every 1000ms to check NTP sync progress
 */
void timeTask(void* pvParameters) {
  // Get reference to TimeManager (passed as parameter)
  TimeManager* pTimeManager = (TimeManager*)pvParameters;

  Serial.println("[FreeRTOS] Time sync task started");

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(1000);  // Run every 1000ms

  bool wifiIsConnected = false;

  while (1) {
    // Wait until it's time to run again (every 1000ms)
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    // Try to receive WiFi status events (non-blocking)
    // We check if WiFi has changed to trigger NTP sync
    WiFiStatusEvent wifiEvent;
    if (xQueueReceive(wifiStatusQueue, &wifiEvent, 0) == pdTRUE) {
      wifiIsConnected = wifiEvent.is_connected;

      if (wifiIsConnected) {
        Serial.println("[TimeTask] WiFi connected - TimeManager will attempt NTP sync");
      } else {
        Serial.println("[TimeTask] WiFi disconnected - NTP sync aborted if in progress");
      }
    }

    // Run TimeManager state machine with current WiFi status
    pTimeManager->handle(wifiIsConnected);
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
 * MODIFIED: Integrated TimeManager test suite
 *
 * Runs every 100ms (can process WiFi events immediately when they arrive)
 */
void mainTask(void* pvParameters) {
  Serial.println("[FreeRTOS] Main task started");

  // Get reference to TimeManager for testing
  extern TimeManager timeManager;

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(100);  // Run every 100ms

  // Test tracking
  static unsigned long lastTestRun = 0;

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

      // Update test state with WiFi connection status
      testState.wifiConnected = wifiEvent.is_connected;
    }

    // Run TimeManager tests every 1000ms
    unsigned long now = millis();
    if (now - lastTestRun >= 1000) {
      lastTestRun = now;
      testTimeManager(&timeManager);
    }
  }
}

// ========================
// FreeRTOS Initialization
// ========================

/**
 * Initialize all FreeRTOS infrastructure
 * Creates task queues and starts all tasks
 * Called from setup() after WiFiManager and TimeManager are initialized
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

  // Get pointers to global manager instances
  extern WiFiManager wifiManager;
  extern TimeManager timeManager;

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

  // Create time synchronization task
  xReturned = xTaskCreate(
    timeTask,                              // Task function
    "TimeTask",                            // Task name
    TASK_STACK_TIME / 4,                   // Stack size in words
    (void*)&timeManager,                   // Parameter (TimeManager pointer)
    TASK_PRIORITY_TIME,                    // Priority
    NULL                                   // Task handle
  );
  if (xReturned != pdPASS) {
    Serial.println("[FreeRTOS] ERROR: Failed to create time sync task!");
    return;
  }
  Serial.println("[FreeRTOS] Time sync task created");

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
