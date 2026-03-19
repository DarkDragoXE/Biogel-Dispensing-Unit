#ifndef VALVES_H
#define VALVES_H

#include <Arduino.h>

// ============================================================================
//  GPIO Pin Assignments — 3 solenoid valves
// ============================================================================
#define PIN_V1_MAT1   25   // Material 1
#define PIN_V2_MAT2   26   // Material 2
#define PIN_V3_CLEAN  27   // Cleaning / purge material
#define PIN_STATUS_LED 2   // Onboard LED

#define NUM_VALVES 3

enum ValveID : uint8_t {
  V1_MAT1  = 0,
  V2_MAT2  = 1,
  V3_CLEAN = 2
};

struct Valve {
  uint8_t    pin;
  const char* name;
  bool        isOpen;
};

static Valve valves[NUM_VALVES] = {
  { PIN_V1_MAT1,  "V1_Mat1",  false },
  { PIN_V2_MAT2,  "V2_Mat2",  false },
  { PIN_V3_CLEAN, "V3_Clean", false }
};

// ============================================================================
//  Valve Control
// ============================================================================
void valves_init() {
  for (int i = 0; i < NUM_VALVES; i++) {
    pinMode(valves[i].pin, OUTPUT);
    digitalWrite(valves[i].pin, LOW);
    valves[i].isOpen = false;
  }
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
}

void valve_open(uint8_t idx) {
  if (idx >= NUM_VALVES) return;
  digitalWrite(valves[idx].pin, HIGH);
  valves[idx].isOpen = true;
}

void valve_close(uint8_t idx) {
  if (idx >= NUM_VALVES) return;
  digitalWrite(valves[idx].pin, LOW);
  valves[idx].isOpen = false;
}

void valve_close_all() {
  for (int i = 0; i < NUM_VALVES; i++) {
    digitalWrite(valves[i].pin, LOW);
    valves[i].isOpen = false;
  }
}

bool valve_is_open(uint8_t idx) {
  return (idx < NUM_VALVES) && valves[idx].isOpen;
}

void valves_print_status() {
  Serial.println("--- Valve Status ---");
  for (int i = 0; i < NUM_VALVES; i++) {
    Serial.printf("  V%d %-10s : %s\n", i + 1, valves[i].name,
                  valves[i].isOpen ? "OPEN" : "closed");
  }
  Serial.println("--------------------");
}

#endif // VALVES_H
