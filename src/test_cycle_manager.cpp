#include "test_cycle_manager.h"
#include "logger.h"
#include "cycle_manager.h"
#include "device_manager.h"
#include "time_manager.h"

extern TimeManager timeManager;

// Helper function to create a time_t value
time_t make_time(int year, int month, int day, int hour, int minute, int second) {
    struct tm t;
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    return mktime(&t);
}

void testCycleManager() {
    Logger::getInstance().logInfo("testCycleManager", "Starting CycleManager tests...");

    CycleManager* cycleManager = CycleManager::getInstance();
    DeviceManager* deviceManager = &DeviceManager::getInstance();
    TimeManager* tm = &timeManager;

    // --- Test 1: Light-on boundary triggering ---
    Logger::getInstance().logInfo("testCycleManager", "--- Test 1: Light-on boundary triggering ---");
    // Set time to 1 minute before a 20-minute boundary
    time_t test_time = make_time(2025, 11, 15, 10, 19, 55);
    tm->setSystemTimeForTesting(test_time);
    // Set light ON
    deviceManager->setState("light_power", 1, "test");
    // Run cycle manager loop
    cycleManager->loop();
    // Advance time by 10 seconds to cross the boundary
    test_time += 10;
    tm->setSystemTimeForTesting(test_time);
    // Run cycle manager loop again
    cycleManager->loop();
    // We expect a cycle to be triggered here. We will verify by checking the logs.
    Logger::getInstance().logInfo("testCycleManager", "Check logs for 'Light ON, on boundary. Triggering cycle.'");


    // --- Test 2: Light-on immediate triggering ---
    Logger::getInstance().logInfo("testCycleManager", "--- Test 2: Light-on immediate triggering ---");
    // Set time to be exactly on a 20-minute boundary
    test_time = make_time(2025, 11, 15, 10, 20, 0);
    tm->setSystemTimeForTesting(test_time);
    // Set light ON
    deviceManager->setState("light_power", 1, "test");
    // Run cycle manager loop
    cycleManager->loop();
    // We expect a cycle to be triggered here. We will verify by checking the logs.
    Logger::getInstance().logInfo("testCycleManager", "Check logs for 'Light ON at boundary. Triggering immediate cycle.'");


    // --- Test 3: Light-off safety feed ---
    Logger::getInstance().logInfo("testCycleManager", "--- Test 3: Light-off safety feed ---");
    // Set light OFF
    deviceManager->setState("light_power", 0, "test");
    // Set time
    test_time = make_time(2025, 11, 15, 10, 21, 0);
    tm->setSystemTimeForTesting(test_time);
    // Run cycle manager loop
    cycleManager->loop();
    // Advance time by 6 hours + 1 second
    test_time += (6 * 3600) + 1;
    tm->setSystemTimeForTesting(test_time);
    // Run cycle manager loop again
    cycleManager->loop();
    // We expect a cycle to be triggered here. We will verify by checking the logs.
    Logger::getInstance().logInfo("testCycleManager", "Check logs for 'Light OFF, safety feed triggered'");


    Logger::getInstance().logInfo("testCycleManager", "CycleManager tests finished.");
}
