// Updated Code (Auto-Hold Edition)
#include <LiquidCrystal.h>

LiquidCrystal lcd(12, 11, 10, 9, 8, 7);

const int pinA = 2;
const int pinB = 3;
const int btnTargetUp = 13;
const int btnTargetDown = 6;
const int btnResetDist = 4;

const int btnHold = 5;  // This is now your "Ready for Next Piece" button

const int ledPin = 44;
const int buzzerPin = 45;

volatile long pulseCount = 0;
float wheelDiameterMM = 50.0;
float mmPerPulse = (3.14159 * wheelDiameterMM) / 20.0;

int targetCM = 10;

int pieceCount = 0;
bool paused = false;
bool targetReached = false;  // New flag for auto-hold logic

unsigned long buttonTimer = 0;
bool longPressTriggered = false;
unsigned long lastHoldPress = 0;

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

  digitalWrite(ledPin, HIGH);
  digitalWrite(buzzerPin, HIGH);
  delay(300);
  digitalWrite(ledPin, LOW);
  digitalWrite(buzzerPin, LOW);

  attachInterrupt(digitalPinToInterrupt(pinA), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB), handleEncoder, CHANGE);

  lcd.print("SAFE-CUT V2.8");
  delay(1000);
  lcd.clear();
}

void loop() {
  float currentCM = (pulseCount * mmPerPulse) / 10.0;

  // 1. AUTO-HOLD LOGIC
  if (currentCM >= (float)targetCM && !targetReached && !paused) {
    pieceCount++;
    targetReached = true;
    paused = true;  // LOCK the measurement immediately
  }

  // 2. TARGET SCALE (Buttons 13 and 6)
  if (digitalRead(btnTargetUp) == LOW) {
    targetCM += 10;
    if (targetCM > 200) targetCM = 10;
    delay(250);
  }
  if (digitalRead(btnTargetDown) == LOW) {
    targetCM -= 10;
    if (targetCM < 10) targetCM = 200;
    delay(250);
  }

  // 3. MANUAL RESET / RELEASE (Button 5)
  // This button now does two things:
  // - If target was reached: It resets the alarm and distance.
  // - If target wasn't reached: It works as a normal Pause.
  if (digitalRead(btnHold) == LOW) {
    if (millis() - lastHoldPress > 500) {
      if (targetReached) {
        pulseCount = 0;
        targetReached = false;
        paused = false;  // Start measuring again
      } else {
        paused = !paused;
      }
      lastHoldPress = millis();
      lcd.clear();
    }
  }

  // 4. LONG PRESS RESET (Button 4)
  if (digitalRead(btnResetDist) == LOW) {
    if (buttonTimer == 0) buttonTimer = millis();
    if (millis() - buttonTimer > 2000 && !longPressTriggered) {
      pieceCount = 0;
      pulseCount = 0;
      targetReached = false;
      paused = false;
      longPressTriggered = true;
      lcd.setCursor(10, 0);
      lcd.print("CLR!");
      delay(600);
      lcd.clear();
    }
  } else {
    if (buttonTimer > 0 && !longPressTriggered) {
      pulseCount = 0;
      targetReached = false;
    }
    buttonTimer = 0;
    longPressTriggered = false;
  }

  // 5. DISPLAY & ALARM
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(targetCM);
  lcd.print("cm ");
  lcd.setCursor(10, 0);
  lcd.print("PC:");
  lcd.print(pieceCount);
  lcd.print("  ");

  lcd.setCursor(0, 1);
  if (targetReached) {
    lcd.print("!!! CUT NOW !!! ");
    // Alarm stays on until you "Release" the system
    if ((millis() / 200) % 2 == 0) {
      digitalWrite(ledPin, HIGH);
      digitalWrite(buzzerPin, HIGH);
    } else {
      digitalWrite(ledPin, LOW);
      digitalWrite(buzzerPin, LOW);
    }
  } else {
    digitalWrite(ledPin, LOW);
    digitalWrite(buzzerPin, LOW);
    if (paused) lcd.print("PAUSED / HOLD   ");
    else {
      lcd.print("NOW: ");
      lcd.print(currentCM, 1);
      lcd.print(" cm      ");
    }
  }
}

void handleEncoder() {
  if (paused) return;  // This is what freezes the number on screen
  static int lastA = LOW;
  int currentA = digitalRead(pinA);
  if (currentA != lastA) {
    if (digitalRead(pinB) != currentA) pulseCount++;
    else pulseCount--;
  }
  lastA = currentA;
}
