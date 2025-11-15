#ifndef CYCLE_MANAGER_H
#define CYCLE_MANAGER_H

#include <Arduino.h>
#include "device_manager.h"
#include "time_manager.h"
#include "logger.h"

class CycleManager {
public:
    static CycleManager* getInstance();
    void setup(const char* configPath);
    void loop(); // Called periodically by the FreeRTOS task

private:
    CycleManager();
    CycleManager(const CycleManager&) = delete;
    CycleManager& operator=(const CycleManager&) = delete;

    DeviceManager* _deviceManager;
    TimeManager* _timeManager;
    Logger* _logger;

    // Configuration
    bool _enabled;
    int _frequencyMinutes; // For light-on mode
    long _maxTimeLightsOffSeconds; // For light-off mode

    // State tracking
    time_t _lastFeedTime;
    bool _lastLightState; // true for ON, false for OFF
    bool _triggeredThisMinute; // To prevent duplicate triggers in light-on mode

    void loadConfiguration(const char* configPath);
    void triggerCycle();
    void executeCycle();
};

#endif // CYCLE_MANAGER_H
