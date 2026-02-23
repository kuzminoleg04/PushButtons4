#include <LiquidCrystal.h>

// Initialize the library with your specific pins
// RS, E, D4, D5, D6, D7
LiquidCrystal lcd(12, 11, 10, 9, 8, 7);

const int btnPins[] = {2, 3, 4, 5}; // Up, Down, Left, Right

void setup() {
  lcd.begin(16, 2);
  lcd.print("System Ready");
  
  for(int i = 0; i < 4; i++) {
    pinMode(btnPins[i], INPUT_PULLUP);
  }
  delay(1000);
  lcd.clear();
}

void loop() {
  if (digitalRead(btnPins[0]) == LOW) updateDisplay("UP Pressed");
  if (digitalRead(btnPins[1]) == LOW) updateDisplay("DOWN Pressed");
  if (digitalRead(btnPins[2]) == LOW) updateDisplay("LEFT Pressed");
  if (digitalRead(btnPins[3]) == LOW) updateDisplay("RIGHT Pressed");
}

void updateDisplay(String msg) {
  lcd.setCursor(0, 0);
  lcd.print("Input Detected: ");
  lcd.setCursor(0, 1);
  lcd.print("                "); // Clear line
  lcd.setCursor(0, 1);
  lcd.print(msg);
  delay(200); // Simple debounce
}