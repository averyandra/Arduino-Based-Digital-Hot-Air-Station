#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- DEBUG LOGGING SWITCH ---
#define DEBUG_ENABLE 1  // Set 1 untuk Aktifkan Serial Debug, 0 untuk Matikan

#if DEBUG_ENABLE
  #define LOG_INIT(baud)       Serial.begin(baud)
  #define LOG_PRINT(x)         Serial.print(x)
  #define LOG_PRINTLN(x)       Serial.println(x)
  #define LOG_PRINTF(str)      Serial.print(F(str))
  #define LOG_PRINTLNF(str)    Serial.println(F(str))
#else
  #define LOG_INIT(baud)
  #define LOG_PRINT(x)
  #define LOG_PRINTLN(x)
  #define LOG_PRINTF(str)
  #define LOG_PRINTLNF(str)
#endif      

// --- PIN MAP ---
const uint8_t BUZZER_PIN        = 6;
const uint8_t TRIAC_PIN         = 7;
const uint8_t HANDLE_SWITCH_PIN = 8;
const uint8_t FAN_PIN           = 9;
const uint8_t ENC_SW            = 3;
const uint8_t ENC_DT            = 4;
const uint8_t ENC_CLK           = 5;
const uint8_t MAX_CS            = 10;
const uint8_t MAX_SO            = 12; // Configured to pin 12 based on hardware schematic
const uint8_t MAX_SCK           = 13;
const uint8_t ZERO_CROSS_PIN    = 2;

// --- SYSTEM CONFIGURATION ---
const uint8_t LCD_ADDRESS = 0x27;
// HIGH = Handle up (Sleep), LOW = Handle down (Work) [tilt sensor version]
// HIGH = Handle outside the Holder (work), LOW Handle in the Holder (Sleep) [magnetic sensor version]
const bool HANDLE_ON_HOLDER_LEVEL = LOW; 
const int MIN_TEMP_C = 100;
const int MAX_TEMP_C = 500;
const int MIN_FAN_PERCENT = 20;
const int MAX_FAN_PERCENT = 100;
const float READY_BAND_C = 5.0f;
const unsigned long BACKLIGHT_TIMEOUT_MS = 60000;
const unsigned long READY_STABLE_MS = 2000;

// --- PID TUNING PARAMETERS ---
const float HEATER_KP = 2.5f;           // Proportional gain
const float HEATER_KI = 0.2f;           // Integral gain (provides 15-25% hold power)
const float HEATER_KD = 1.0f;           // Derivative gain
const float PID_INTEGRAL_MAX = 200.0f;  // Anti-windup integral clamp limit
const float PID_ACTIVE_BAND_C = 30.0f;  // Active integral band limit (+/- 30 C from setpoint)

// --- MACHINE STATE ---
enum UiState : uint8_t { UI_BOOT, UI_FAULT, UI_SET_TEMP, UI_SET_FAN, UI_CONFIRM, UI_RUN };

// --- GLOBAL VARIABLES (EXTERN) ---
extern UiState uiState;
extern float currentTempC;
extern int targetTempC;
extern int savedTargetTempC;
extern int fanPercent;
extern int savedFanPercent;
extern int heaterDemand;
extern bool sensorOK;
extern bool sleeping;
extern bool backlightOn;
extern bool confirmYes;
extern bool lcdNeedsRefresh;
extern unsigned long lastActivity;
extern volatile unsigned long lastZeroCrossMicros;

#endif