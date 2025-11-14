#ifndef FREERTOS_TASKS_H
#define FREERTOS_TASKS_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "wifi_manager.h"

// ========================
// Task Priorities
// ========================
// FreeRTOS priorities: 0 (lowest) to configMAX_PRIORITIES-1 (highest)
// ESP32 typically has configMAX_PRIORITIES = 25
#define TASK_PRIORITY_WIFI 2        // WiFi management task
#define TASK_PRIORITY_TIME 2        // Time synchronization task
#define TASK_PRIORITY_NETLOG 2      // Network logging task
#define TASK_PRIORITY_MAIN 3        // Main orchestration task
#define TASK_PRIORITY_SENSOR 1      // Sensor reading task (placeholder)
#define TASK_PRIORITY_CONTROL 1     // Control logic task (placeholder)
#define TASK_PRIORITY_LOGGING 0     // Logging task (lowest priority)

// ========================
// Task Stack Sizes
// ========================
// Stack sizes in words (each word is 4 bytes on ESP32)
// NOTE: Increased for testing - revert after TimeManager tests complete
#define TASK_STACK_WIFI (8 * 1024)      // 8KB for WiFi task
#define TASK_STACK_TIME (8 * 1024)      // 8KB for time sync task (increased for testing)
#define TASK_STACK_NETLOG (16 * 1024)   // 16KB for network logging task (increased for debugging stack overflow issues)
#define TASK_STACK_MAIN (16 * 1024)     // 16KB for main task (increased for OTA - ArduinoOTA.begin() needs significant stack)
#define TASK_STACK_SENSOR (4 * 1024)    // 4KB for sensor task
#define TASK_STACK_CONTROL (4 * 1024)   // 4KB for control task

// ========================
// Queue Message Types
// ========================

// WiFi Status Event - Published by WiFiTask when connection state changes
struct WiFiStatusEvent {
  WiFiConnectionState state;      // Current connection state
  WiFiOperatingMode mode;         // Current operating mode (STATION or AP)
  bool is_connected;              // Whether WiFi is connected
  unsigned long timestamp;        // When the event occurred
  char ssid[33];                  // Current SSID (if applicable)
  int8_t rssi;                    // Signal strength (if applicable)
};

// Network Command - Published by NetworkLogger when command received from server
struct NetworkCommand {
  char type[16];        // Command type (e.g., "status", "reboot")
  char payload[128];    // Optional JSON payload
  unsigned long timestamp;  // When command was received
};

// ========================
// Queue Handles (declared globally)
// ========================

extern QueueHandle_t wifiStatusQueue;
extern QueueHandle_t networkCommandQueue;

// ========================
// Task Function Declarations
// ========================

/**
 * WiFi Management Task
 * Runs WiFiManager state machine at regular intervals
 * Publishes WiFi status events to queue when state changes
 */
void wifiTask(void* pvParameters);

/**
 * Time Synchronization Task
 * Handles NTP synchronization and time management
 * Subscribes to WiFi status to trigger NTP sync when available
 * Runs every 1000ms
 */
void timeTask(void* pvParameters);

/**
 * Network Logger Task
 * Handles TCP log synchronization with timestamp calculation
 * Reads from active.log, sends to TCP server, waits for acks
 * Sends heartbeats every 1s of idle time
 * Receives commands from server and queues to application
 * Runs on core 0
 */
void networkLoggerTask(void* pvParameters);

/**
 * Main Orchestration Task
 * Subscribes to WiFi status queue and network command queue
 * Handles OTA updates and task management
 * Replaces traditional loop() with event-driven architecture
 */
void mainTask(void* pvParameters);

/**
 * Initialize all FreeRTOS infrastructure
 * Creates task queues and starts all tasks
 * Called from setup()
 */
void initializeFreeRTOS();

#endif // FREERTOS_TASKS_H
