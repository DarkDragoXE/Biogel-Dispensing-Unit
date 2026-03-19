# Biogel Dispensing Unit

ESP32-based bioink dispensing system for bioprinting. Controls 3 solenoid valves and dual Peltier temperature modules with PID control and NTC thermistor feedback.

## Hardware

- **MCU:** ESP32-WROOM-32
- **Valves:** 3x 12V solenoid valves via MOSFET drivers
- **Temperature:** Up to 3x Peltier modules (heating/cooling) via MOSFET + DPDT relay
- **Sensors:** 100K NTC thermistors (one per Peltier head + optional chamber)

## Pin Assignments

### Solenoid Valves
| Valve | GPIO | Function |
|---|---|---|
| V1 | 25 | Material 1 |
| V2 | 26 | Material 2 |
| V3 | 27 | Cleaning / Purge |

### Peltier Modules
| Module | PWM (MOSFET) | DPDT | Thermistor |
|---|---|---|---|
| P0 | GPIO 16 | GPIO 17 | GPIO 35 |
| P1 | GPIO 18 | GPIO 19 | GPIO 34 |
| P2 (reserved) | GPIO 21 | GPIO 22 | GPIO 36 |

### Thermistor Wiring
```
3.3V ── 100K resistor ── GPIO ── NTC thermistor ── GND
```

### DPDT Logic
- `HIGH` → DPDT energized → **HEATING**
- `LOW`  → DPDT relaxed   → **COOLING**

## Serial Commands (115200 baud)

### Valve Control
```
DISPENSE1 <ms>      Open Material 1 valve for <ms> milliseconds
DISPENSE2 <ms>      Open Material 2 valve for <ms> milliseconds
CLEAN <ms>          Open cleaning valve for <ms> milliseconds
VALVE <1-3> ON|OFF  Manually open/close a valve
```

### Peltier Control
```
P0 OFF              Turn off Peltier 0
P0 POLARITY HEAT    Set to heating (DPDT HIGH)
P0 POLARITY COOL    Set to cooling (DPDT LOW)
P0 PWM <0-255>      Set power level manually
P0 TARGET <temp>    PID auto-control to target °C
PSTOP               Emergency stop all Peltiers
```
> Same commands apply for P1 and P2

### Temperature
```
TEMP                    Read all thermistors now
TEMP REPORT ON|OFF      Toggle continuous temperature output
TEMP INTERVAL <ms>      Set report interval (min 500ms)
```

### System
```
STATUS              Show all valve + Peltier states
STOP                Emergency stop everything
HELP                Show commands on device
```

## Enabling More Peltiers

Change one line in `peltier.h`:
```cpp
#define NUM_PELTIERS_ACTIVE 1   // Change to 2 or 3
```

## Safety

- DPDT polarity switches only after PWM drops to 0 (100ms interlock)
- Watchdog closes all valves if no serial activity for 30s
- Max dispense duration: 10 seconds per command
