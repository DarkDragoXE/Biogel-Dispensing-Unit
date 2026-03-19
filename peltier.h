#ifndef PELTIER_H
#define PELTIER_H

#include <Arduino.h>

// ============================================================================
//  Peltier Temperature Control (up to 3 modules)
//  Compatible with ESP32 Arduino core v3.x (ledcAttach / ledcWrite by pin)
//
//  Circuit (same for each Peltier):
//    ESP32 PWM pin  → MOSFET gate → SMPS power → DPDT relay input
//    ESP32 DPDT pin → DPDT relay coil  (HIGH = HEAT, LOW = COOL)
//    DPDT relay     → Peltier
//    Thermistor     → 3.3V → 100K resistor → GPIO → NTC → GND
//
//  Pin assignments:
//    P0:  PWM=GPIO16  DPDT=GPIO17  TEMP=GPIO34   ← ACTIVE (material head 0)
//    P1:  PWM=GPIO18  DPDT=GPIO19  TEMP=GPIO35   ← reserved (material head 1)
//    P2:  PWM=GPIO21  DPDT=GPIO22  TEMP=GPIO36   ← reserved (future)
//    Chamber thermistor: GPIO36 (monitored, no Peltier)
//
//  To activate more Peltiers: increase NUM_PELTIERS_ACTIVE (max 3).
// ============================================================================

// ============================================================================
//  Configuration
// ============================================================================

#define NUM_PELTIERS        3
#define NUM_PELTIERS_ACTIVE 1   // Change to 2 or 3 to enable more

static const uint8_t PELTIER_PWM_PINS[NUM_PELTIERS]  = { 16, 18, 21 };
static const uint8_t PELTIER_DPDT_PINS[NUM_PELTIERS] = { 17, 19, 22 };
static const uint8_t PELTIER_TEMP_PINS[NUM_PELTIERS] = { 35, 34, 36 };

// Thermistor (100K NTC @ 25°C)
#define THERMISTOR_NOMINAL     100000
#define TEMPERATURE_NOMINAL    25
#define B_COEFFICIENT          3950
#define SERIES_RESISTOR        100000
#define ADC_MAX                4095

// LEDC PWM (core v3.x: ledcAttach uses pin directly, no channel needed)
#define LEDC_FREQ    1000   // 1 kHz
#define LEDC_RES     8      // 8-bit → 0-255

// Safety interlock
#define PELTIER_INTERLOCK_DELAY_MS  100

// PID
#define PID_KP          10.0f
#define PID_KI           0.5f
#define PID_KD           2.0f
#define PID_INTERVAL_MS  500

// Chamber thermistor (monitoring only)
#define THERMISTOR_CHAMBER_PIN    36
#define THERMISTOR_CHAMBER_ACTIVE false   // Set to true when chamber thermistor is wired

// Temperature reporting
#define TEMP_REPORT_INTERVAL_DEFAULT_MS  2000

// ============================================================================
//  Data types
// ============================================================================

enum PeltierMode : uint8_t {
  PELTIER_OFF,
  PELTIER_HEATING,
  PELTIER_COOLING,
  PELTIER_TARGET
};

struct PeltierState {
  uint8_t   pwm_pin;
  uint8_t   dpdt_pin;
  uint8_t   temp_pin;

  PeltierMode mode;
  bool        heating;
  uint8_t     pwm;
  float       target_temp;

  float         pid_integral;
  float         pid_last_error;
  unsigned long pid_last_ms;

  float current_temp;
};

static PeltierState peltier[NUM_PELTIERS];

static float         chamber_temp             = 0.0f;
static bool          temp_report_enabled      = true;
static unsigned long temp_report_interval_ms  = TEMP_REPORT_INTERVAL_DEFAULT_MS;
static unsigned long last_temp_report_ms      = 0;

// ============================================================================
//  Thermistor — Steinhart-Hart Beta equation
// ============================================================================
float readThermistor(int pin) {
  // Average 16 samples to reduce ESP32 ADC noise
  long sum = 0;
  for (int i = 0; i < 16; i++) sum += analogRead(pin);
  int raw = sum / 16;

  if (raw <= 0) return -999.0f;

  float resistance = (float)SERIES_RESISTOR / ((float)ADC_MAX / (float)raw - 1.0f);
  if (resistance <= 0) return -999.0f;

  float steinhart = log(resistance / (float)THERMISTOR_NOMINAL) / (float)B_COEFFICIENT
                  + 1.0f / ((float)TEMPERATURE_NOMINAL + 273.15f);

  return (1.0f / steinhart) - 273.15f;
}

// ============================================================================
//  Internal helpers
// ============================================================================
static const char* _pmode_str(const PeltierState& p) {
  switch (p.mode) {
    case PELTIER_OFF:     return "OFF";
    case PELTIER_HEATING: return "HEAT";
    case PELTIER_COOLING: return "COOL";
    case PELTIER_TARGET:  return p.heating ? "TARGET-HEAT" : "TARGET-COOL";
    default:              return "?";
  }
}

static void _peltier_apply(PeltierState& p) {
  if (p.mode == PELTIER_OFF) {
    ledcWrite(p.pwm_pin, 0);
    digitalWrite(p.dpdt_pin, LOW);
    return;
  }
  digitalWrite(p.dpdt_pin, p.heating ? HIGH : LOW);
  delay(PELTIER_INTERLOCK_DELAY_MS);
  ledcWrite(p.pwm_pin, p.pwm);
}

// ============================================================================
//  Safe mode change with interlock sequence
// ============================================================================
void peltier_set_mode(PeltierState& p, PeltierMode mode, bool heating, uint8_t pwm) {
  bool was_on          = (p.mode != PELTIER_OFF);
  bool direction_change = (p.heating != heating);

  if (was_on && (direction_change || mode == PELTIER_OFF)) {
    ledcWrite(p.pwm_pin, 0);
    delay(PELTIER_INTERLOCK_DELAY_MS);
    digitalWrite(p.dpdt_pin, LOW);
    delay(PELTIER_INTERLOCK_DELAY_MS);
  }

  p.mode           = mode;
  p.heating        = heating;
  p.pwm            = pwm;
  p.pid_integral   = 0.0f;
  p.pid_last_error = 0.0f;
  p.pid_last_ms    = 0;

  if (mode != PELTIER_OFF) {
    _peltier_apply(p);
  }
}

// ============================================================================
//  Initialization — call from setup()
// ============================================================================
void peltier_init() {
  for (int i = 0; i < NUM_PELTIERS_ACTIVE; i++) {
    PeltierState& p = peltier[i];

    p.pwm_pin         = PELTIER_PWM_PINS[i];
    p.dpdt_pin        = PELTIER_DPDT_PINS[i];
    p.temp_pin        = PELTIER_TEMP_PINS[i];
    p.mode            = PELTIER_OFF;
    p.heating         = false;
    p.pwm             = 0;
    p.target_temp     = 25.0f;
    p.pid_integral    = 0.0f;
    p.pid_last_error  = 0.0f;
    p.pid_last_ms     = 0;
    p.current_temp    = 0.0f;

    // ESP32 core v3.x LEDC API: ledcAttach(pin, freq, resolution)
    ledcAttach(p.pwm_pin, LEDC_FREQ, LEDC_RES);
    ledcWrite(p.pwm_pin, 0);

    pinMode(p.dpdt_pin, OUTPUT);
    digitalWrite(p.dpdt_pin, LOW);

    pinMode(p.temp_pin, INPUT);
    p.current_temp = readThermistor(p.temp_pin);
  }

  pinMode(THERMISTOR_CHAMBER_PIN, INPUT);
  chamber_temp = readThermistor(THERMISTOR_CHAMBER_PIN);
}

// ============================================================================
//  PID step — internal, non-blocking
// ============================================================================
static void _peltier_pid_update(PeltierState& p) {
  unsigned long now = millis();
  if ((now - p.pid_last_ms) < PID_INTERVAL_MS) return;
  p.pid_last_ms = now;

  if (p.current_temp < -100.0f) return;

  if ( p.heating && p.current_temp >= p.target_temp) { ledcWrite(p.pwm_pin, 0); p.pwm = 0; return; }
  if (!p.heating && p.current_temp <= p.target_temp) { ledcWrite(p.pwm_pin, 0); p.pwm = 0; return; }

  // Error is always positive: how far we are from target regardless of direction
  float error = p.heating ? (p.target_temp - p.current_temp)
                           : (p.current_temp - p.target_temp);
  float dt    = PID_INTERVAL_MS / 1000.0f;

  p.pid_integral += error * dt;
  p.pid_integral  = constrain(p.pid_integral, -200.0f, 200.0f);

  float deriv = (error - p.pid_last_error) / dt;
  p.pid_last_error = error;

  float output = PID_KP * error + PID_KI * p.pid_integral + PID_KD * deriv;
  output = constrain(output, 0.0f, 255.0f);

  p.pwm = (uint8_t)output;
  ledcWrite(p.pwm_pin, p.pwm);
}

// ============================================================================
//  Non-blocking update — call every loop()
// ============================================================================
void peltier_update() {
  for (int i = 0; i < NUM_PELTIERS_ACTIVE; i++) {
    peltier[i].current_temp = readThermistor(peltier[i].temp_pin);
    if (peltier[i].mode == PELTIER_TARGET) {
      _peltier_pid_update(peltier[i]);
    }
  }
  chamber_temp = readThermistor(THERMISTOR_CHAMBER_PIN);
}

// ============================================================================
//  Continuous temperature report — call every loop()
// ============================================================================
void reportTemperatures() {
  unsigned long now = millis();
  if (!temp_report_enabled) return;
  if ((now - last_temp_report_ms) < temp_report_interval_ms) return;
  last_temp_report_ms = now;

  Serial.print("TEMP >> ");
  for (int i = 0; i < NUM_PELTIERS_ACTIVE; i++) {
    PeltierState& p = peltier[i];
    if (i > 0) Serial.print("  |  ");
    if (p.current_temp > -100.0f)
      Serial.printf("P%d:%.1fC [%s", i, p.current_temp, _pmode_str(p));
    else
      Serial.printf("P%d:ERR [", i);
    if (p.mode != PELTIER_OFF) {
      if (p.mode == PELTIER_TARGET) Serial.printf(" tgt:%.1fC", p.target_temp);
      Serial.printf(" PWM:%d", p.pwm);
    }
    Serial.print("]");
  }
  if (THERMISTOR_CHAMBER_ACTIVE) {
    Serial.print("  |  ");
    if (chamber_temp > -100.0f) Serial.printf("Chamber:%.1fC", chamber_temp);
    else                         Serial.print("Chamber:ERR");
  }
  Serial.println();
}

// ============================================================================
//  Emergency stop
// ============================================================================
void peltier_emergency_stop() {
  for (int i = 0; i < NUM_PELTIERS_ACTIVE; i++) {
    PeltierState& p = peltier[i];
    ledcWrite(p.pwm_pin, 0);
    delay(PELTIER_INTERLOCK_DELAY_MS);
    digitalWrite(p.dpdt_pin, LOW);
    p.mode = PELTIER_OFF;
    p.pwm  = 0;
  }
  Serial.println("Peltier STOP - all off");
}

// ============================================================================
//  Status print
// ============================================================================
void peltier_print_status() {
  Serial.println("--- Peltier Status ---");
  for (int i = 0; i < NUM_PELTIERS_ACTIVE; i++) {
    PeltierState& p = peltier[i];
    Serial.printf("  P%d: %-12s", i, _pmode_str(p));
    if (p.current_temp > -100.0f) Serial.printf("  Temp: %.1fC", p.current_temp);
    else                           Serial.print("  Temp: ERR");
    if (p.mode != PELTIER_OFF) {
      if (p.mode == PELTIER_TARGET) Serial.printf("  Target: %.1fC", p.target_temp);
      Serial.printf("  PWM: %d/255", p.pwm);
    }
    Serial.println();
  }
  if (THERMISTOR_CHAMBER_ACTIVE) {
    if (chamber_temp > -100.0f) Serial.printf("  Chamber: %.1fC\n", chamber_temp);
    else                         Serial.println("  Chamber: ERR");
  } else {
    Serial.println("  Chamber: not connected");
  }
  Serial.println("----------------------");
}

// ============================================================================
//  Command parser
// ============================================================================
bool peltier_process_command(const char* cmd) {

  if (strcasecmp(cmd, "PSTOP") == 0) {
    peltier_emergency_stop();
    return true;
  }

  if (strcasecmp(cmd, "TEMP") == 0) {
    for (int i = 0; i < NUM_PELTIERS_ACTIVE; i++) {
      int raw = analogRead(peltier[i].temp_pin);
      float t = readThermistor(peltier[i].temp_pin);
      if (t > -100.0f) Serial.printf("T%d (mat head %d): %.2f C  [raw ADC: %d]\n", i, i, t, raw);
      else             Serial.printf("T%d (mat head %d): ERR  [raw ADC: %d — expect 500-3500 if wired correctly]\n", i, i, raw);
    }
    if (THERMISTOR_CHAMBER_ACTIVE) {
      int rawc = analogRead(THERMISTOR_CHAMBER_PIN);
      float tc = readThermistor(THERMISTOR_CHAMBER_PIN);
      if (tc > -100.0f) Serial.printf("T2 (chamber):     %.2f C  [raw ADC: %d]\n", tc, rawc);
      else              Serial.printf("T2 (chamber):     ERR  [raw ADC: %d]\n", rawc);
    } else {
      Serial.println("T2 (chamber):     not connected");
    }
    return true;
  }

  if (strcasecmp(cmd, "TEMP REPORT ON") == 0) {
    temp_report_enabled = true;
    Serial.printf("Temp reporting ON (every %lu ms)\n", temp_report_interval_ms);
    return true;
  }
  if (strcasecmp(cmd, "TEMP REPORT OFF") == 0) {
    temp_report_enabled = false;
    Serial.println("Temp reporting OFF");
    return true;
  }
  if (strncasecmp(cmd, "TEMP INTERVAL ", 14) == 0) {
    unsigned long ms = strtoul(cmd + 14, NULL, 10);
    if (ms >= 500) {
      temp_report_interval_ms = ms;
      Serial.printf("Temp report interval: %lu ms\n", ms);
    } else {
      Serial.println("ERROR: Minimum interval is 500ms");
    }
    return true;
  }

  // P<n> commands
  if ((cmd[0] == 'P' || cmd[0] == 'p') && cmd[1] >= '0' && cmd[1] <= '2' && cmd[2] == ' ') {
    int idx = cmd[1] - '0';
    const char* subcmd = cmd + 3;

    if (idx >= NUM_PELTIERS_ACTIVE) {
      Serial.printf("ERROR: P%d not active. Increase NUM_PELTIERS_ACTIVE in peltier.h\n", idx);
      return true;
    }

    PeltierState& p = peltier[idx];

    if (strcasecmp(subcmd, "OFF") == 0) {
      peltier_set_mode(p, PELTIER_OFF, false, 0);
      Serial.printf("P%d: OFF\n", idx);
      return true;
    }

    if (strncasecmp(subcmd, "POLARITY ", 9) == 0) {
      const char* dir = subcmd + 9;
      uint8_t pwm = (p.pwm > 0) ? p.pwm : 128;
      if (strcasecmp(dir, "HEAT") == 0) {
        peltier_set_mode(p, PELTIER_HEATING, true, pwm);
        Serial.printf("P%d: HEATING PWM=%d (DPDT HIGH)\n", idx, p.pwm);
      } else if (strcasecmp(dir, "COOL") == 0) {
        peltier_set_mode(p, PELTIER_COOLING, false, pwm);
        Serial.printf("P%d: COOLING PWM=%d (DPDT LOW)\n", idx, p.pwm);
      } else {
        Serial.printf("ERROR: Usage: P%d POLARITY HEAT|COOL\n", idx);
      }
      return true;
    }

    if (strncasecmp(subcmd, "PWM ", 4) == 0) {
      p.pwm = (uint8_t)constrain(atoi(subcmd + 4), 0, 255);
      if (p.mode != PELTIER_OFF) ledcWrite(p.pwm_pin, p.pwm);
      Serial.printf("P%d: PWM = %d\n", idx, p.pwm);
      return true;
    }

    if (strncasecmp(subcmd, "TARGET ", 7) == 0) {
      float target  = atof(subcmd + 7);
      float current = readThermistor(p.temp_pin);
      bool  heat    = (target > current);

      p.target_temp    = target;
      p.pid_integral   = 0.0f;
      p.pid_last_error = 0.0f;
      p.pid_last_ms    = 0;
      p.mode           = PELTIER_TARGET;
      p.heating        = heat;

      ledcWrite(p.pwm_pin, 0);
      delay(PELTIER_INTERLOCK_DELAY_MS);
      digitalWrite(p.dpdt_pin, heat ? HIGH : LOW);
      delay(PELTIER_INTERLOCK_DELAY_MS);

      Serial.printf("P%d: TARGET %.1fC  (current %.1fC, %s)\n",
                    idx, target, current, heat ? "HEATING" : "COOLING");
      return true;
    }

    Serial.printf("ERROR: Unknown P%d command. Try: OFF | POLARITY HEAT|COOL | PWM <n> | TARGET <temp>\n", idx);
    return true;
  }

  return false;
}

#endif // PELTIER_H
