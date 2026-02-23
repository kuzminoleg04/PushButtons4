#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const int pinA = 2;
const int pinB = 3;
const int btnTargetUp = 4;
const int btnTargetDown = 5;
const int btnResetDist = 6;
const int btnHold = 7;

const int ledPin = 8;
const int buzzerPin = 9;
const int relayPin = 10;      // Cutter relay control
const int feedMotorPin = 11;  // Feed motor relay control

// Set to HIGH for active-high relay modules, LOW for active-low modules.
const int relayActiveLevel = HIGH;
const int feedMotorActiveLevel = HIGH;

volatile long pulseCount = 0;
float wheelDiameterMM = 50.0;
float mmPerPulse = (3.14159f * wheelDiameterMM) / 20.0f;

int targetCM = 10;
int pieceCount = 0;
int targetPieces = 0;  // 0 = infinite run

bool paused = false;
bool cutInProgress = false;
bool cutTimeoutFault = false;
bool editPiecesMode = false;

unsigned long cutStartMs = 0;
unsigned long lastCutMs = 0;
unsigned long buttonTimer = 0;
unsigned long lastTargetUpPress = 0;
unsigned long lastTargetDownPress = 0;
unsigned long holdPressStartMs = 0;
bool holdWasDown = false;
bool longPressTriggered = false;

const unsigned long debounceMs = 180;
unsigned long cutRelayOnMs = 250;         // Relay ON duration for one cut
unsigned long cutTimeoutMs = 1200;        // Full system stop time during each cut
const unsigned long rearmDelayMs = 150;   // Prevent immediate retrigger after reset
const unsigned long holdLongPressMs = 1200;
const unsigned long cutSafetyTimeoutMs = 30000;  // Failsafe max time in cut state
const unsigned long resetLongPressMs = 2000;

long readPulseCountAtomic();
void resetPulseCountAtomic();
float pulsesToCm(long pulses);
void setRelay(bool on);
void setFeedMotor(bool on);
bool pieceGoalReached();
void validateTimingConfig();
void updateFeedMotor(bool goalReached);
void startCut(unsigned long nowMs);
void finishCut(unsigned long nowMs);
void enterCutTimeoutFault();
void handleCutState(unsigned long nowMs);
void processAutoCut(unsigned long nowMs, float currentCM, bool goalReached);
void handleTargetButtons(unsigned long nowMs);
void handleHoldButton(unsigned long nowMs);
void handleResetButton(unsigned long nowMs);
void updateStatusOutputs(unsigned long nowMs, bool goalReached);
void renderDisplay(float currentCM, bool goalReached);

long readPulseCountAtomic() {
  noInterrupts();
  long snapshot = pulseCount;
  interrupts();
  return snapshot;
}

void resetPulseCountAtomic() {
  noInterrupts();
  pulseCount = 0;
  interrupts();
}

float pulsesToCm(long pulses) {
  return (pulses * mmPerPulse) / 10.0f;
}

void setRelay(bool on) {
  digitalWrite(relayPin, on ? relayActiveLevel : !relayActiveLevel);
}

void setFeedMotor(bool on) {
  digitalWrite(feedMotorPin, on ? feedMotorActiveLevel : !feedMotorActiveLevel);
}

bool pieceGoalReached() {
  return (targetPieces > 0) && (pieceCount >= targetPieces);
}

void validateTimingConfig() {
  if (cutRelayOnMs < 1) cutRelayOnMs = 1;
  if (cutTimeoutMs < cutRelayOnMs) cutTimeoutMs = cutRelayOnMs;
}

void updateFeedMotor(bool goalReached) {
  bool shouldRun = !paused && !cutInProgress && !cutTimeoutFault && !goalReached;
  setFeedMotor(shouldRun);
}

void startCut(unsigned long nowMs) {
  cutInProgress = true;
  cutTimeoutFault = false;
  cutStartMs = nowMs;
  setFeedMotor(false);  // Synchronize: always stop feed while cutting
  setRelay(true);
}

void finishCut(unsigned long nowMs) {
  setRelay(false);
  cutInProgress = false;
  lastCutMs = nowMs;
  pieceCount++;
  if (pieceGoalReached()) paused = true;

  // Reset measured distance for the next cable piece automatically.
  resetPulseCountAtomic();
}

void enterCutTimeoutFault() {
  setRelay(false);
  setFeedMotor(false);
  cutInProgress = false;
  cutTimeoutFault = true;
  paused = true;
}

void handleCutState(unsigned long nowMs) {
  if (!cutInProgress) return;

  unsigned long elapsed = nowMs - cutStartMs;

  // Failsafe: if cut state hangs too long, latch a fault.
  if (elapsed >= cutSafetyTimeoutMs) {
    enterCutTimeoutFault();
    return;
  }

  // Turn cutter relay OFF after actuator pulse time.
  if (elapsed >= cutRelayOnMs) {
    setRelay(false);
  }

  // Keep whole machine stopped for cutTimeoutMs, then resume cycle.
  if (elapsed >= cutTimeoutMs) {
    finishCut(nowMs);
  }
}

void processAutoCut(unsigned long nowMs, float currentCM, bool goalReached) {
  if (paused || cutInProgress || cutTimeoutFault || goalReached) return;
  if (nowMs - lastCutMs < rearmDelayMs) return;
  if (currentCM >= (float)targetCM) startCut(nowMs);
}

void handleTargetButtons(unsigned long nowMs) {
  if (digitalRead(btnTargetUp) == LOW && nowMs - lastTargetUpPress >= debounceMs) {
    if (editPiecesMode) {
      targetPieces += 1;
      if (targetPieces > 999) targetPieces = 0;
    } else {
      targetCM += 1;
      if (targetCM > 500) targetCM = 1;
    }
    lastTargetUpPress = nowMs;
    lcd.clear();
  }

  if (digitalRead(btnTargetDown) == LOW && nowMs - lastTargetDownPress >= debounceMs) {
    if (editPiecesMode) {
      if (targetPieces == 0) targetPieces = 999;
      else targetPieces -= 1;
    } else {
      targetCM -= 1;
      if (targetCM < 1) targetCM = 500;
    }
    lastTargetDownPress = nowMs;
    lcd.clear();
  }
}

void handleHoldButton(unsigned long nowMs) {
  bool holdDown = (digitalRead(btnHold) == LOW);
  if (holdDown && !holdWasDown) {
    holdPressStartMs = nowMs;
  } else if (!holdDown && holdWasDown) {
    unsigned long pressDuration = nowMs - holdPressStartMs;
    if (pressDuration >= holdLongPressMs) {
      editPiecesMode = !editPiecesMode;
    } else {
      paused = !paused;
      if (!paused) cutTimeoutFault = false;
    }
    lcd.clear();
  }
  holdWasDown = holdDown;
}

void handleResetButton(unsigned long nowMs) {
  if (digitalRead(btnResetDist) == LOW) {
    if (buttonTimer == 0) buttonTimer = nowMs;

    if ((nowMs - buttonTimer > resetLongPressMs) && !longPressTriggered) {
      resetPulseCountAtomic();
      pieceCount = 0;
      cutTimeoutFault = false;
      paused = false;
      longPressTriggered = true;
      setRelay(false);
      cutInProgress = false;
      lcd.clear();
    }
  } else {
    if (buttonTimer > 0 && !longPressTriggered) {
      resetPulseCountAtomic();
    }
    buttonTimer = 0;
    longPressTriggered = false;
  }
}

void updateStatusOutputs(unsigned long nowMs, bool goalReached) {
  bool alertBlink = ((nowMs / 180) % 2 == 0);
  digitalWrite(ledPin, (cutInProgress || cutTimeoutFault) && alertBlink ? HIGH : LOW);
  digitalWrite(buzzerPin, cutTimeoutFault && alertBlink ? HIGH : LOW);
  updateFeedMotor(goalReached);
}

void renderDisplay(float currentCM, bool goalReached) {
  lcd.setCursor(0, 0);
  lcd.print("L:");
  lcd.print(targetCM);
  lcd.print(" ");
  lcd.print("PC:");
  lcd.print(pieceCount);
  lcd.print(" ");

  lcd.setCursor(0, 1);
  if (editPiecesMode) {
    lcd.print("SET PCS:");
    if (targetPieces == 0) lcd.print("INF");
    else lcd.print(targetPieces);
    lcd.print("      ");
  } else if (cutTimeoutFault) {
    lcd.print("CUT TIMEOUT!    ");
  } else if (cutInProgress) {
    lcd.print("CUTTING...      ");
  } else if (goalReached) {
    lcd.print("BATCH DONE      ");
  } else if (paused) {
    lcd.print("PAUSED          ");
  } else {
    lcd.print("NOW:");
    lcd.print(currentCM, 1);
    lcd.print("cm ");
    if (targetPieces == 0) lcd.print("INF");
    else {
      lcd.print(pieceCount);
      lcd.print("/");
      lcd.print(targetPieces);
    }
    lcd.print("   ");
  }
}

void setup() {
  lcd.init();
  lcd.backlight();

  pinMode(pinA, INPUT_PULLUP);
  pinMode(pinB, INPUT_PULLUP);
  pinMode(btnTargetUp, INPUT_PULLUP);
  pinMode(btnTargetDown, INPUT_PULLUP);
  pinMode(btnResetDist, INPUT_PULLUP);
  pinMode(btnHold, INPUT_PULLUP);

  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  pinMode(feedMotorPin, OUTPUT);

  setRelay(false);
  setFeedMotor(false);

  digitalWrite(ledPin, HIGH);
  digitalWrite(buzzerPin, HIGH);
  delay(200);
  digitalWrite(ledPin, LOW);
  digitalWrite(buzzerPin, LOW);

  attachInterrupt(digitalPinToInterrupt(pinA), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB), handleEncoder, CHANGE);

  lcd.print("AUTO-CUT UNO");
  delay(900);
  lcd.clear();
}

void loop() {
  unsigned long nowMs = millis();
  validateTimingConfig();
  handleCutState(nowMs);

  long pulses = readPulseCountAtomic();
  float currentCM = pulsesToCm(pulses);
  bool goalReached = pieceGoalReached();
  if (goalReached) paused = true;

  processAutoCut(nowMs, currentCM, goalReached);
  handleTargetButtons(nowMs);
  handleHoldButton(nowMs);
  handleResetButton(nowMs);
  updateStatusOutputs(nowMs, goalReached);
  renderDisplay(currentCM, goalReached);
}

void handleEncoder() {
  if (paused || cutInProgress || pieceGoalReached()) return;

  static int lastA = LOW;
  int currentA = digitalRead(pinA);

  if (currentA != lastA) {
    if (digitalRead(pinB) != currentA) pulseCount++;
    else pulseCount--;
  }

  lastA = currentA;
}
