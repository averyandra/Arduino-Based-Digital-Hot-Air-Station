#include "Config.h"
#include "Hardware.h"
#include "UI.h"

// --- GLOBAL VARIABLES INITIALIZATION ---
UiState uiState = UI_BOOT;
float currentTempC = NAN;
int targetTempC = 350;
int savedTargetTempC = 350;
int fanPercent = 50;
int savedFanPercent = 50;
int heaterDemand = 0;

bool sensorOK = false;
bool sleeping = false;
bool backlightOn = true;
bool confirmYes = true;
bool lcdNeedsRefresh = true;
unsigned long lastActivity = 0;

// Main Loop Specific Variables
unsigned long bootStarted = 0;
unsigned long lastTempRead = 0;
unsigned long lastUiRefresh = 0;
unsigned long lastDebugTelemetry = 0; // Timer untuk Serial Log periodik
unsigned long readySince = 0;
bool readyBeeped = false;

// PID Controller State Variables
static float pidIntegral = 0.0f;
static float pidLastError = 0.0f;

void resetPID() {
  pidIntegral = 0.0f;
  pidLastError = 0.0f;
}

void setup() {
  LOG_INIT(115200);
  LOG_PRINTLNF("\n=================================");
  LOG_PRINTLNF("   HOT AIR STATION BOOTING...    ");
  LOG_PRINTLNF("=================================");

  hardwareInit();
  uiInit();
  
  bootStarted = millis();
  lastActivity = millis();

  LOG_PRINTF("[SYS] System Initialized. Free RAM: ");
  LOG_PRINT(getFreeRam());
  LOG_PRINTLNF(" bytes");
}

void updateSystemLogic() {
  // 1. Backlight Timeout
  if (uiState != UI_FAULT && backlightOn && (millis() - lastActivity >= BACKLIGHT_TIMEOUT_MS)) {
    backlightOn = false; 
    lcdNeedsRefresh = true;
  }

  if (uiState != UI_RUN) return;

  // 2. Sleep State (Tilt/Holder Switch)
  bool nowSleeping = handleIsOnHolder();
  if (nowSleeping && !sleeping) {
    sleeping = true;
    heaterOff();
    resetPID();
    queueBeep(1);
    lcdNeedsRefresh = true;
    LOG_PRINTLNF("[SYS LOGIC] Handle placed on holder -> SLEEP MODE");
  } else if (!nowSleeping && sleeping) {
    sleeping = false;
    readySince = 0;
    readyBeeped = false;
    resetPID();
    lcdNeedsRefresh = true;
    LOG_PRINTLNF("[SYS LOGIC] Handle picked up -> WORK MODE");
  }

  // 3. Fan Cooling System
  if (sleeping) {
    heaterOff();
    resetPID();
    if (sensorOK && currentTempC > 100.0f) analogWrite(FAN_PIN, 255); 
    else analogWrite(FAN_PIN, 0); 
  } else {
    analogWrite(FAN_PIN, map(fanPercent, 0, 100, 0, 255));
  }

  // 4. Heater PID Control
  bool zcHealthy = (lastZeroCrossMicros != 0UL) && ((micros() - lastZeroCrossMicros) < 30000UL);
  
  if (!sensorOK || sleeping || !zcHealthy) {
    if (!zcHealthy && !sleeping && sensorOK) {
      LOG_PRINTLNF("[WARNING] Zero-Cross signal lost/unhealthy! Heater shut off.");
    }
    heaterOff();
    resetPID();
  } else {
    float error = (float)targetTempC - currentTempC;

    if (fabs(error) <= PID_ACTIVE_BAND_C) {
      pidIntegral += error * 0.25f;
      pidIntegral = constrain(pidIntegral, 0.0f, PID_INTEGRAL_MAX);
    } else {
      pidIntegral = 0.0f;
    }

    float derivative = (error - pidLastError) / 0.25f;
    pidLastError = error;

    float pTerm = HEATER_KP * error;
    float iTerm = HEATER_KI * pidIntegral;
    float dTerm = HEATER_KD * derivative;

    float output = pTerm + iTerm + dTerm;
    int demand = constrain((int)output, 0, 100);

    setHeaterDemand(demand);
  }

  // 5. Ready Status & Beep
  if (!sleeping && sensorOK) {
    bool inBand = fabs(currentTempC - (float)targetTempC) <= READY_BAND_C;
    if (inBand) {
      if (readySince == 0) readySince = millis();
      if (!readyBeeped && (millis() - readySince >= READY_STABLE_MS)) {
        readyBeeped = true;
        queueBeep(1);
        lcdNeedsRefresh = true;
        LOG_PRINTLNF("[SYS LOGIC] Temperature STABLE -> Target Reached!");
      }
    } else {
      readySince = 0;
    }
  }
}

void loop() {
  unsigned long now = millis();

  updateEncoder();
  updateButton();
  updateBuzzer();

  // Polling Suhu (250ms)
  if (uiState != UI_FAULT && (now - lastTempRead >= 250)) {
    lastTempRead = now;

    if (uiState == UI_BOOT) {
      if (now - bootStarted >= 3000) {
        if (readTemperature()) {
          queueBeep(1);
          enterRunMode();
        } else if (now - bootStarted >= 4500) {
          LOG_PRINTLNF("[BOOT ERROR] Sensor read failed during boot up!");
          enterFault(); 
        }
      }
    } else {
      if (!readTemperature()) enterFault();
    }
  }

  // System State Handler
  if (uiState == UI_FAULT) {
    heaterOff();
    resetPID();
    analogWrite(FAN_PIN, 0);
  } else {
    updateSystemLogic();
  }

  // UI Refresh Interval (200ms)
  if (now - lastUiRefresh >= 200) {
    lastUiRefresh = now;
    if (lcdNeedsRefresh) {
      refreshLCD();
      lcdNeedsRefresh = false;
    } else if (uiState == UI_RUN /* && backlightOn */ ) {
      refreshLCD(); 
    }
  }

  // TELEMETRY LOGGING VIA SERIAL (Setiap 1000 ms / 1 Detik)
#if DEBUG_ENABLE
  if (now - lastDebugTelemetry >= 1000) {
    lastDebugTelemetry = now;

    LOG_PRINTF("[TELEMETRY] T_Act: ");
    LOG_PRINT(isnan(currentTempC) ? 0 : (int)currentTempC);
    LOG_PRINTF("C | T_Set: ");
    LOG_PRINT(targetTempC);
    LOG_PRINTF("C | Fan: ");
    LOG_PRINT(fanPercent);
    LOG_PRINTF("% | H_Demand: ");
    LOG_PRINT(heaterDemand);
    LOG_PRINTF("% | State: ");
    
    switch (uiState) {
      case UI_BOOT: LOG_PRINTF("BOOT"); break;
      case UI_FAULT: LOG_PRINTF("FAULT"); break;
      case UI_SET_TEMP: LOG_PRINTF("SET_TEMP"); break;
      case UI_SET_FAN: LOG_PRINTF("SET_FAN"); break;
      case UI_CONFIRM: LOG_PRINTF("CONFIRM"); break;
      case UI_RUN: LOG_PRINTF(sleeping ? "RUN(SLEEP)" : "RUN(WORK)"); break;
    }

    LOG_PRINTF(" | ZC: ");
    bool zcOk = (lastZeroCrossMicros != 0UL) && ((micros() - lastZeroCrossMicros) < 30000UL);
    LOG_PRINT(zcOk ? "OK" : "MISSING");

    LOG_PRINTF(" | FreeRAM: ");
    LOG_PRINT(getFreeRam());
    LOG_PRINTLNF("B");
  }
#endif
}