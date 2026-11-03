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

uint32_t lastActivity = 0;

uint32_t bootStarted = 0;
uint32_t lastTempRead = 0;
uint32_t lastUiRefresh = 0;
uint32_t readySince = 0;

// Hysteresis tracking variables
bool readyBeeped = false;
int lastBeepTargetTempC = 350; 

static float pidIntegral = 0.0f;
static float pidLastTemp = 0.0f;
static uint32_t lastPidTime = 0;

void resetPID() {
  pidIntegral = 0.0f;
  pidLastTemp = currentTempC;
  lastPidTime = millis();
}

void updateSystemLogic() {
  uint32_t now = millis();
  
  if (uiState != UI_FAULT && backlightOn && (now - lastActivity >= BACKLIGHT_TIMEOUT_MS)) {
    backlightOn = false;
    lcdNeedsRefresh = true;
  }

  if (uiState != UI_RUN) return;

  bool nowSleeping = handleIsOnHolder();
  if (nowSleeping && !sleeping) {
    sleeping = true;
    heaterOff();
    resetPID();
    queueBeep(1);
    lcdNeedsRefresh = true;
  } else if (!nowSleeping && sleeping) {
    sleeping = false;
    readySince = 0;
    readyBeeped = false;
    resetPID();
    lcdNeedsRefresh = true;
  }

  if (sleeping) {
    heaterOff();
    resetPID();
    if (sensorOK && currentTempC > 100.0f) setFanPWM(255);
    else setFanPWM(0);
  } else {
    uint8_t fanDuty = (uint8_t)((uint32_t)fanPercent * 255 / 100);
    setFanPWM(fanDuty);
  }

  bool zcHealthy = (lastZeroCrossMicros != 0UL) && ((micros() - lastZeroCrossMicros) < 30000UL);
  
  if (!sensorOK || sleeping || !zcHealthy) {
    heaterOff();
    resetPID();
  } else {
    // PID Computation - Gated strictly to 250ms to prevent integral windup 
    if (now - lastPidTime >= 250) {
      float dt = (float)(now - lastPidTime) / 1000.0f;
      lastPidTime = now;

      float error = (float)targetTempC - currentTempC;
      
      // Calculate integral with active band to avoid deep saturation
      if (fabs(error) <= PID_ACTIVE_BAND_C) {
        pidIntegral += HEATER_KI * error * dt;
        if (pidIntegral < 0.0f) pidIntegral = 0.0f;
        else if (pidIntegral > PID_INTEGRAL_MAX) pidIntegral = PID_INTEGRAL_MAX;
      } else {
        pidIntegral = 0.0f;
      }

      // Derivative on Measurement (prevents derivative kick on setpoint change)
      float derivative = -HEATER_KD * (currentTempC - pidLastTemp) / dt;
      pidLastTemp = currentTempC;

      float pTerm = HEATER_KP * error;
      float output = pTerm + pidIntegral + derivative;
      
      int demand = (output < 0.0f) ? 0 : ((output > 100.0f) ? 100 : (int)output);
      setHeaterDemand(demand);
    }
  }

  // Ready Beep Logic with Hysteresis
  if (targetTempC != lastBeepTargetTempC) {
    lastBeepTargetTempC = targetTempC;
    readyBeeped = false; // Reset beep if user changes the target
    readySince = 0;
  }

  if (!sleeping && sensorOK) {
    float tempError = (float)targetTempC - currentTempC;
    bool inBand = fabs(tempError) <= READY_BAND_C;
    
    if (inBand) {
      if (readySince == 0) readySince = now;
      if (!readyBeeped && (now - readySince >= READY_STABLE_MS)) {
        readyBeeped = true;
        queueBeep(1);
        lcdNeedsRefresh = true;
      }
    } else {
      readySince = 0;
      // Only reset the ready beep flag if the temp drops significantly below the target
      // This stops the repeated beeping if it oscillates a few degrees below the band.
      if (tempError > READY_HYSTERESIS_C) {
        readyBeeped = false;
      }
    }
  }
}

int main(void) {
  hardwareInit();
  uiInit();

  bootStarted = millis();
  lastActivity = millis();
  lastPidTime = millis();

  // Infinite Loop
  while (1) {
    uint32_t now = millis();

    updateEncoder();
    updateButton();
    updateBuzzer();

    if (uiState == UI_BOOT) {
      if (now - bootStarted >= 3000) {
        if (readTemperature()) {
          queueBeep(1);
          enterRunMode();
        } else {
          enterFault();
        }
      }
    }

    if (uiState != UI_BOOT && uiState != UI_FAULT && (now - lastTempRead >= 250)) {
      lastTempRead = now;
      if (!readTemperature()) enterFault();
    }

    if (uiState == UI_FAULT) {
      heaterOff();
      resetPID();
      setFanPWM(0);
    } else {
      updateSystemLogic();
    }

    if (now - lastUiRefresh >= 200) {
      lastUiRefresh = now;
      if (lcdNeedsRefresh) {
        refreshLCD();
        lcdNeedsRefresh = false;
      } else if (uiState == UI_RUN) {
        refreshLCD();
      }
    }
  }
  return 0;
}