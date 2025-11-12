#ifndef TEST_TIME_MANAGER_H
#define TEST_TIME_MANAGER_H

#include "time_manager.h"

/**
 * Time Manager Test Utilities
 *
 * Provides helper functions for testing TimeManager functionality
 * Can be included in main.cpp and called from mainTask via serial commands
 */

class TimeManagerTester {
public:
  /**
   * Print all time and sync information
   */
  static void printFullStatus(TimeManager* tm) {
    if (!tm) return;

    Serial.println("\n=== TIME MANAGER FULL STATUS ===");

    // Current time
    char timeStr[64];
    tm->getTimeString(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S UTC", false);
    Serial.print("UTC Time:     ");
    Serial.println(timeStr);

    tm->getTimeString(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S %Z", true);
    Serial.print("Local Time:   ");
    Serial.println(timeStr);

    // Confidence state
    Serial.print("Confidence:   ");
    Serial.println(tm->isTimeConfident() ? "CONFIDENT" : "UNCONFIDENT");

    // Last sync
    time_t lastSync = tm->getLastSyncTime();
    if (lastSync > 0) {
      struct tm syncTime = {};
      localtime_r(&lastSync, &syncTime);
      Serial.printf("Last Sync:    %04d-%02d-%02d %02d:%02d:%02d\n",
          syncTime.tm_year + 1900, syncTime.tm_mon + 1, syncTime.tm_mday,
          syncTime.tm_hour, syncTime.tm_min, syncTime.tm_sec);

      uint32_t msSince = tm->getMillisSinceSyncTime();
      Serial.printf("Time Since:   %lu ms (%.1f hours)\n", msSince, msSince / 3600000.0f);
    } else {
      Serial.println("Last Sync:    Never");
    }

    // Timezone
    Serial.printf("Timezone:     %s\n", tm->getTimezone());

    // Log
    Serial.printf("Event Log:    %d entries\n", tm->getLogEntryCount());

    Serial.println("=== END STATUS ===\n");
  }

  /**
   * Print time sync event log
   */
  static void printEventLog(TimeManager* tm) {
    if (!tm) return;
    tm->printTimeLog();
  }

  /**
   * Verify timezone is set correctly
   */
  static bool verifyTimezone(TimeManager* tm, const char* expectedTZ) {
    if (!tm || !expectedTZ) return false;

    Serial.print("[TEST] Verifying timezone: ");
    Serial.print(expectedTZ);
    Serial.print(" ... ");

    const char* actual = tm->getTimezone();
    if (strcmp(actual, expectedTZ) == 0) {
      Serial.println("PASS");
      return true;
    } else {
      Serial.print("FAIL (got: ");
      Serial.print(actual);
      Serial.println(")");
      return false;
    }
  }

  /**
   * Verify time is confidence-worthy (after NTP sync)
   */
  static bool verifyTimeConfident(TimeManager* tm) {
    Serial.print("[TEST] Verifying time confidence ... ");

    if (tm->isTimeConfident()) {
      Serial.println("PASS");
      return true;
    } else {
      Serial.println("FAIL (time not confident)");
      return false;
    }
  }

  /**
   * Verify time is reasonable (year >= 2025)
   */
  static bool verifyTimeReasonable(TimeManager* tm) {
    Serial.print("[TEST] Verifying time is reasonable ... ");

    time_t now = tm->getTime();
    struct tm timeinfo = {};
    gmtime_r(&now, &timeinfo);

    // Year should be 2025 or later
    if (timeinfo.tm_year >= 125) {  // 125 = 2025 - 1900
      Serial.printf("PASS (year: %d)\n", timeinfo.tm_year + 1900);
      return true;
    } else {
      Serial.printf("FAIL (year: %d, expected >= 2025)\n", timeinfo.tm_year + 1900);
      return false;
    }
  }

  /**
   * Verify last sync time exists
   */
  static bool verifyLastSyncExists(TimeManager* tm) {
    Serial.print("[TEST] Verifying last sync time ... ");

    time_t lastSync = tm->getLastSyncTime();
    if (lastSync > 0) {
      struct tm syncTime = {};
      localtime_r(&lastSync, &syncTime);
      Serial.printf("PASS (sync: %04d-%02d-%02d %02d:%02d:%02d)\n",
          syncTime.tm_year + 1900, syncTime.tm_mon + 1, syncTime.tm_mday,
          syncTime.tm_hour, syncTime.tm_min, syncTime.tm_sec);
      return true;
    } else {
      Serial.println("FAIL (no sync recorded)");
      return false;
    }
  }
};

#endif // TEST_TIME_MANAGER_H
