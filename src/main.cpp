#include <Arduino.h>
#include <SPIFFS.h>
#include "wifi_manager.h"

WiFiManager wifiManager;

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
  Serial.println("=== Hydromatic System Starting ===");

  // Initialize SPIFFS filesystem
  Serial.println("Setup(): Initializing SPIFFS...");
  Serial.flush();
  if (!SPIFFS.begin(true)) {
    Serial.println("ERROR: SPIFFS initialization failed!");
    Serial.flush();
    return;
  }
  Serial.println("Setup(): SPIFFS initialized successfully.");
  Serial.flush();

  // Initialize WiFi Manager with config from SPIFFS
  Serial.println("Setup(): Initializing WiFiManager...");
  Serial.println("Setup(): Calling wifiManager.begin()...");
  Serial.flush();
  wifiManager.begin("/config.json");

  Serial.println("Setup: WiFiManager initialized successfully.");
  Serial.flush();
}

void loop() {
  // Call WiFiManager's handle() to process state machine
  wifiManager.handle();

  // Heartbeat: print every 1 second to show device is alive
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat >= 1000) {
    lastHeartbeat = millis();
    Serial.print(".");
    Serial.flush();
  }

  // Print status every 10 seconds
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 10000) {
    lastPrint = millis();
    Serial.println();
    Serial.print("LOOP: millis=");
    Serial.println(millis());
    wifiManager.printStatus();
    Serial.println();
  }

  // Allow small delay to avoid watchdog timeout
  delay(100);
}
