// ============================================================================
//  BioGel ESP32 - Bioink Dispensing System Controller
//
//  Hardware: ESP32-WROOM-32
//    - 3x solenoid valves via MOSFET drivers
//    - 1x Peltier module (heating/cooling) via MOSFET + DPDT relay  [P1, P2 reserved]
//    - 1x 100K NTC thermistor
//
//  Valve Pins:
//    V1 (GPIO25) → Material 1
//    V2 (GPIO26) → Material 2
//    V3 (GPIO27) → Cleaning / purge material
//
//  Peltier Pins (P0 active):
//    GPIO16 → MOSFET gate   → Peltier PWM power
//    GPIO17 → DPDT coil     → HIGH = HEAT, LOW = COOL
//    GPIO34 → Thermistor    → 100K NTC (3.3V → 100K resistor → GPIO34 → NTC → GND)
//
//  Valve Commands:
//    DISPENSE1 <ms>      Open Material 1 valve for <ms> milliseconds
//    DISPENSE2 <ms>      Open Material 2 valve for <ms> milliseconds
//    CLEAN <ms>          Open cleaning valve for <ms> milliseconds
//    VALVE <1-3> ON|OFF  Manually open/close a valve
//
//  Peltier Commands:
//    P0 OFF              Turn off Peltier 0
//    P0 POLARITY HEAT    DPDT HIGH → heating
//    P0 POLARITY COOL    DPDT LOW  → cooling
//    P0 PWM <0-255>      Set power level
//    P0 TARGET <temp>    PID auto-control to target °C
//    PSTOP               Emergency stop Peltier(s)
//
//  Temperature Commands:
//    TEMP                Read thermistor(s) now
//    TEMP REPORT ON|OFF  Toggle continuous temperature output
//    TEMP INTERVAL <ms>  Set report interval (min 500ms, default 2000ms)
//
//  System:
//    STATUS              Show all states
//    STOP                Emergency stop everything
//    HELP                Show all commands
// ============================================================================

#include "valves.h"
#include "dispenser.h"
#include "peltier.h"

#define SERIAL_BAUD       115200
#define INPUT_BUFFER_SIZE 64

char    inputBuffer[INPUT_BUFFER_SIZE];
uint8_t inputPos = 0;

// ============================================================================
//  Setup
// ============================================================================
void setup() {
  Serial.begin(SERIAL_BAUD);

  unsigned long waitStart = millis();
  while (!Serial && (millis() - waitStart < 3000)) delay(10);

  valves_init();
  peltier_init();

  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_STATUS_LED, HIGH); delay(100);
    digitalWrite(PIN_STATUS_LED, LOW);  delay(100);
  }

  Serial.println();
  Serial.println("=========================================");
  Serial.println("  BioGel ESP32 - Dispensing Controller");
  Serial.println("  3-Valve + Peltier P0 Ready");
  Serial.println("=========================================");
  Serial.println("Type HELP for available commands");
  Serial.println();

  valves_print_status();
  peltier_print_status();

  lastSerialActivity = millis();
}

// ============================================================================
//  Command Parser
// ============================================================================
void processCommand(const char* cmd) {
  lastSerialActivity = millis();
  while (*cmd == ' ') cmd++;
  if (strlen(cmd) == 0) return;

  // --- STOP ---
  if (strcasecmp(cmd, "STOP") == 0) {
    dispense_stop();
    peltier_emergency_stop();
    return;
  }

  // --- STATUS ---
  if (strcasecmp(cmd, "STATUS") == 0) {
    valves_print_status();
    peltier_print_status();
    if (dispense_is_busy()) Serial.println("  ** Dispense in progress **");
    return;
  }

  // --- HELP ---
  if (strcasecmp(cmd, "HELP") == 0) {
    Serial.println("--- BioGel Commands ---");
    Serial.println();
    Serial.println("  Valves:");
    Serial.println("    DISPENSE1 <ms>      Open Material 1 for <ms> ms");
    Serial.println("    DISPENSE2 <ms>      Open Material 2 for <ms> ms");
    Serial.println("    CLEAN <ms>          Open cleaning valve for <ms> ms");
    Serial.println("    VALVE <1-3> ON|OFF  Manual valve control");
    Serial.println();
    Serial.println("  Peltier:");
    Serial.println("    P0 OFF              Turn off");
    Serial.println("    P0 POLARITY HEAT    Set to heating (DPDT HIGH)");
    Serial.println("    P0 POLARITY COOL    Set to cooling (DPDT LOW)");
    Serial.println("    P0 PWM <0-255>      Set power level");
    Serial.println("    P0 TARGET <temp>    PID auto-control to target C");
    Serial.println("    PSTOP               Emergency stop Peltier only");
    Serial.println();
    Serial.println("  Temperature:");
    Serial.println("    TEMP                Read thermistor now");
    Serial.println("    TEMP REPORT ON|OFF  Toggle continuous output");
    Serial.println("    TEMP INTERVAL <ms>  Set interval (min 500ms)");
    Serial.println();
    Serial.println("  System:");
    Serial.println("    STATUS  STOP  HELP");
    Serial.println("-----------------------");
    return;
  }

  // --- DISPENSE1 <ms> ---
  if (strncasecmp(cmd, "DISPENSE1 ", 10) == 0) {
    dispense_start(1, strtoul(cmd + 10, NULL, 10));
    return;
  }
  if (strcasecmp(cmd, "DISPENSE1") == 0) {
    Serial.println("ERROR: Usage: DISPENSE1 <ms>");
    return;
  }

  // --- DISPENSE2 <ms> ---
  if (strncasecmp(cmd, "DISPENSE2 ", 10) == 0) {
    dispense_start(2, strtoul(cmd + 10, NULL, 10));
    return;
  }
  if (strcasecmp(cmd, "DISPENSE2") == 0) {
    Serial.println("ERROR: Usage: DISPENSE2 <ms>");
    return;
  }

  // --- CLEAN <ms> ---
  if (strncasecmp(cmd, "CLEAN ", 6) == 0) {
    dispense_start(3, strtoul(cmd + 6, NULL, 10));
    return;
  }
  if (strcasecmp(cmd, "CLEAN") == 0) {
    Serial.println("ERROR: Usage: CLEAN <ms>");
    return;
  }

  // --- VALVE <n> ON/OFF ---
  if (strncasecmp(cmd, "VALVE ", 6) == 0) {
    int  valveNum = 0;
    char action[8] = {0};
    if (sscanf(cmd + 6, "%d %7s", &valveNum, action) == 2) {
      if (valveNum < 1 || valveNum > NUM_VALVES) {
        Serial.printf("ERROR: Valve must be 1-%d\n", NUM_VALVES);
        return;
      }
      uint8_t idx = valveNum - 1;
      if (strcasecmp(action, "ON") == 0) {
        if (dispense_is_busy()) {
          Serial.println("ERROR: Dispense in progress. Send STOP first.");
          return;
        }
        valve_open(idx);
        Serial.printf("V%d %s: OPENED\n", valveNum, valves[idx].name);
      } else if (strcasecmp(action, "OFF") == 0) {
        valve_close(idx);
        Serial.printf("V%d %s: CLOSED\n", valveNum, valves[idx].name);
      } else {
        Serial.println("ERROR: Usage: VALVE <1-3> ON|OFF");
      }
    } else {
      Serial.println("ERROR: Usage: VALVE <1-3> ON|OFF");
    }
    return;
  }

  // --- Peltier / Temperature commands ---
  if (peltier_process_command(cmd)) return;

  Serial.printf("ERROR: Unknown command '%s'. Type HELP.\n", cmd);
}

// ============================================================================
//  Main Loop
// ============================================================================
void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (inputPos > 0) {
        inputBuffer[inputPos] = '\0';
        processCommand(inputBuffer);
        inputPos = 0;
      }
    } else if (inputPos < INPUT_BUFFER_SIZE - 1) {
      inputBuffer[inputPos++] = c;
    } else {
      Serial.println("ERROR: Command too long");
      inputPos = 0;
    }
  }

  dispense_update();
  peltier_update();
  reportTemperatures();
}
