#include "cycle_manager.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

extern TimeManager timeManager;

// Static instance for Singleton pattern
CycleManager* CycleManager::getInstance() {
    static CycleManager instance;
    return &instance;
}

CycleManager::CycleManager() :
    _deviceManager(&DeviceManager::getInstance()),
    _timeManager(&timeManager),
    _logger(&Logger::getInstance()),
    _enabled(false),
    _frequencyMinutes(20),
    _maxTimeLightsOffSeconds(6 * 3600),
    _lastFeedTime(0),
    _lastLightState(false),
    _triggeredThisMinute(false) {
    // Constructor - initializations
}

void CycleManager::setup(const char* configPath) {
    loadConfiguration(configPath);
    _logger->logInfo("CycleManager", "Setup complete. Enabled: %s", _enabled ? "true" : "false");

    // Initialize last_feed_time to current time on setup to prevent immediate safety feed
    _lastFeedTime = _timeManager->getTime();
    // Initialize last_light_state from current device state
    _lastLightState = _deviceManager->getState("light_power");
}

void CycleManager::loadConfiguration(const char* configPath) {
    _logger->logInfo("CycleManager", "Loading config from %s", configPath);

    File configFile = SPIFFS.open(configPath, "r");
    if (!configFile) {
        _logger->logError("CycleManager", "Could not open %s", configPath);
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        _logger->logError("CycleManager", "JSON parse failed: %s", error.c_str());
        return;
    }

    JsonObject cycleManagerConfig = doc["cycle_manager"];
    if (!cycleManagerConfig) {
        _logger->logError("CycleManager", "No 'cycle_manager' section in config");
        return;
    }

    _enabled = cycleManagerConfig["enabled"] | true;
    _frequencyMinutes = cycleManagerConfig["frequency_minutes"] | 20;
    long max_time_lights_off_hours = cycleManagerConfig["max_time_lights_off_hours"] | 6;
    _maxTimeLightsOffSeconds = max_time_lights_off_hours * 3600;

    _logger->logInfo("CycleManager", "Config loaded: enabled=%s, frequency=%dmin, max_off_time=%ldhrs",
                  _enabled ? "true" : "false", _frequencyMinutes, max_time_lights_off_hours);
}

void CycleManager::loop() {
    if (!_enabled) {
        return;
    }

    time_t now = _timeManager->getTime();
    struct tm timeinfo;
    _timeManager->getTimeInfo(&timeinfo);
    int currentMinute = timeinfo.tm_min;

    // Get current light state from Device Manager
    bool currentLightState = _deviceManager->getState("light_power");

    // --- Light-ON Mode: Boundary Timing ---
    if (currentLightState) {
        // Check for light state change from OFF to ON
        if (!_lastLightState) {
            _logger->logInfo("CycleManager", "Light turned ON. Checking for immediate cycle.");
            // Light just turned on, check if current minute is a boundary
            if (currentMinute % _frequencyMinutes == 0) {
                _logger->logInfo("CycleManager", "Light ON at boundary. Triggering immediate cycle.");
                triggerCycle();
                _triggeredThisMinute = true; // Mark as triggered for this minute
            } else {
                _logger->logInfo("CycleManager", "Light ON off-boundary. Waiting for next boundary.");
                _triggeredThisMinute = false; // Reset for next boundary
            }
        } else {
            // Light was already ON, check for boundary
            if (currentMinute % _frequencyMinutes == 0) {
                if (!_triggeredThisMinute) {
                    _logger->logInfo("CycleManager", "Light ON, on boundary. Triggering cycle.");
                    triggerCycle();
                    _triggeredThisMinute = true;
                }
            } else {
                _triggeredThisMinute = false; // Reset for next boundary
            }
        }
    }
    // --- Light-OFF Mode: Safety Feed ---
    else { // currentLightState is OFF
        // Reset triggeredThisMinute when light is off
        _triggeredThisMinute = false;

        // Check for light state change from ON to OFF (optional logging)
        if (_lastLightState) {
            _logger->logInfo("CycleManager", "Light turned OFF.");
        }

        time_t timeSinceLastFeed = now - _lastFeedTime;
        if (timeSinceLastFeed >= _maxTimeLightsOffSeconds) {
            _logger->logInfo("CycleManager", "Light OFF, safety feed triggered (last feed %ld seconds ago).", timeSinceLastFeed);
            triggerCycle();
        }
    }

    // Update last light state for the next loop iteration
    _lastLightState = currentLightState;
}

void CycleManager::triggerCycle() {
    _logger->logInfo("CycleManager", "Triggering cycle...");
    executeCycle();
    _lastFeedTime = _timeManager->getTime(); // Update last feed time after execution
    _logger->logInfo("CycleManager", "Cycle triggered and last feed time updated.");
}

void CycleManager::executeCycle() {
    // STUB: Simple pump on/off
    _logger->logInfo("CycleManager", "Executing cycle: turning feed_pump ON for 60 seconds.");
    _deviceManager->setState("feed_pump", 1, "cycle_manager");
    delay(60000); // delay expects milliseconds
    _deviceManager->setState("feed_pump", 0, "cycle_manager");
    _logger->logInfo("CycleManager", "Cycle complete.");
}
