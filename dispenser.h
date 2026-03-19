#ifndef DISPENSER_H
#define DISPENSER_H

#include "valves.h"

// ============================================================================
//  Configuration
// ============================================================================
#define MAX_DISPENSE_MS      10000   // Safety: max 10 seconds per dispense
#define WATCHDOG_TIMEOUT_MS  30000   // Auto-close if no serial for 30s (0=disabled)

// ============================================================================
//  Dispenser State Machine
// ============================================================================
enum DispenseState : uint8_t {
  IDLE,
  DISPENSING
};

struct DispenseJob {
  DispenseState state;
  uint8_t       valve_idx;      // Which valve is open
  unsigned long startTime;
  unsigned long duration;
};

static DispenseJob   currentJob         = { IDLE, 0, 0, 0 };
static unsigned long lastSerialActivity = 0;

// ============================================================================
//  Core Functions
// ============================================================================

// Start a timed open on any valve (1=Mat1, 2=Mat2, 3=Clean)
bool dispense_start(uint8_t material, unsigned long durationMs) {
  if (currentJob.state != IDLE) {
    Serial.println("ERROR: Operation in progress. Send STOP first.");
    return false;
  }

  if (material < 1 || material > NUM_VALVES) {
    Serial.printf("ERROR: Material must be 1-%d\n", NUM_VALVES);
    return false;
  }

  if (durationMs == 0 || durationMs > MAX_DISPENSE_MS) {
    Serial.printf("ERROR: Duration must be 1-%d ms\n", MAX_DISPENSE_MS);
    return false;
  }

  uint8_t idx = material - 1;
  valve_open(idx);

  currentJob.state     = DISPENSING;
  currentJob.valve_idx = idx;
  currentJob.startTime = millis();
  currentJob.duration  = durationMs;

  Serial.printf("DISPENSING %s for %lu ms\n", valves[idx].name, durationMs);
  digitalWrite(PIN_STATUS_LED, HIGH);
  return true;
}

// Emergency stop — close everything immediately
void dispense_stop() {
  valve_close_all();
  currentJob.state = IDLE;
  digitalWrite(PIN_STATUS_LED, LOW);
  Serial.println("STOPPED - All valves closed");
}

// ============================================================================
//  Non-blocking Update — call from loop()
// ============================================================================
void dispense_update() {
  unsigned long now = millis();

  if (currentJob.state == DISPENSING) {
    if (now - currentJob.startTime >= currentJob.duration) {
      valve_close(currentJob.valve_idx);
      currentJob.state = IDLE;
      digitalWrite(PIN_STATUS_LED, LOW);
      Serial.printf("DONE - %s closed\n", valves[currentJob.valve_idx].name);
    }
  }

  // Watchdog
  #if WATCHDOG_TIMEOUT_MS > 0
    if (currentJob.state != IDLE &&
        lastSerialActivity > 0 &&
        (now - lastSerialActivity > WATCHDOG_TIMEOUT_MS)) {
      Serial.println("WATCHDOG: No serial activity - emergency stop!");
      dispense_stop();
    }
  #endif
}

bool dispense_is_busy() {
  return currentJob.state != IDLE;
}

#endif // DISPENSER_H
