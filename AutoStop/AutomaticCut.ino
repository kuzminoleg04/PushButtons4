#include <LiquidCrystal.h>

LiquidCrystal lcd(12, 11, 10, 9, 8, 7);

const int pinA = 2;
const int pinB = 3;
const int btnTargetUp = 13;
const int btnTargetDown = 6;
const int btnResetDist = 4;
const int btnHold = 5;

const int ledPin = 44;
const int buzzerPin = 45;
const int relayPin = 46;  // Connect your relay input here
const int feedMotorPin = 47;  // Feed motor driver/relay control pin

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

long readPulseCountAtomic() {
  noInterrupts();
  long snapshot = pulseCount;
  interrupts();
  return snapshot;
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

void updateFeedMotor() {
  bool shouldRun = !paused && !cutInProgress && !cutTimeoutFault && !pieceGoalReached();
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
  noInterrupts();
  pulseCount = 0;
  interrupts();
}

void handleCutState(unsigned long nowMs) {
  if (!cutInProgress) return;

  unsigned long elapsed = nowMs - cutStartMs;

  // Turn cutter relay OFF after actuator pulse time.
  if (elapsed >= cutRelayOnMs) {
    setRelay(false);
  }

  // Keep whole machine stopped for cutTimeoutMs, then resume cycle.
  if (elapsed >= cutTimeoutMs) {
    finishCut(nowMs);
  }
}

void setup() {
  lcd.begin(16, 2);

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

  lcd.print("AUTO-CUT V1.0");
  delay(900);
  lcd.clear();
}

void loop() {
  unsigned long nowMs = millis();
  handleCutState(nowMs);

  long pulses = readPulseCountAtomic();
  float currentCM = (pulses * mmPerPulse) / 10.0f;
  bool goalReached = pieceGoalReached();
  if (goalReached) paused = true;

  // Auto cut at every target length (x cm), unless paused/fault/cutting.
  if (!paused && !cutInProgress && !cutTimeoutFault && !goalReached && (nowMs - lastCutMs >= rearmDelayMs)) {
    if (currentCM >= (float)targetCM) {
      startCut(nowMs);
    }
  }

  // Target + button
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

  // Target - button
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

  // HOLD button:
  // short press -> pause/resume
  // long press  -> toggle piece-target edit mode
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

  // RESET DIST button: short press reset distance, long press reset all counters.
  if (digitalRead(btnResetDist) == LOW) {
    if (buttonTimer == 0) buttonTimer = nowMs;

    if ((nowMs - buttonTimer > 2000) && !longPressTriggered) {
      noInterrupts();
      pulseCount = 0;
      interrupts();
      pieceCount = 0;
      cutTimeoutFault = false;
      paused = false;
      longPressTriggered = true;
      lcd.clear();
    }
  } else {
    if (buttonTimer > 0 && !longPressTriggered) {
      noInterrupts();
      pulseCount = 0;
      interrupts();
    }
    buttonTimer = 0;
    longPressTriggered = false;
  }

  // Status outputs
  bool alertBlink = ((nowMs / 180) % 2 == 0);
  digitalWrite(ledPin, (cutInProgress || cutTimeoutFault) && alertBlink ? HIGH : LOW);
  digitalWrite(buzzerPin, cutTimeoutFault && alertBlink ? HIGH : LOW);
  updateFeedMotor();

  // LCD
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
