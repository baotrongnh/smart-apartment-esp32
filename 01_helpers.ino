void printLine(LiquidCrystal_I2C& lcd, uint8_t row, const String& text) {
  char line[LCD_COLS + 1];
  snprintf(line, sizeof(line), "%-16.16s", text.c_str());
  lcd.setCursor(0, row);
  lcd.print(line);
}

String stars(uint8_t n) {
  String s = "";
  for (uint8_t i = 0; i < n; i++) {
    s += "*";
  }
  return s;
}

void setScreens(bool on) {
  if (screensOn == on) return;

  screensOn = on;
  if (on) {
    lcdMain.backlight();
    lcdDoor.backlight();
  } else {
    lcdMain.noBacklight();
    lcdDoor.noBacklight();
  }
}

void onUserAction() {
  lastUserActionMs = millis();
  if (!screensOn) {
    setScreens(true);
  }
}

void handleScreenTimeout() {
  if (!screensOn) return;

  if (millis() - lastUserActionMs >= SCREEN_TIMEOUT_MS) {
    setScreens(false);
  }
}

void setDoorMessage(const String& line1, const String& line2, unsigned long holdMs) {
  doorMsg1 = line1;
  doorMsg2 = line2;
  doorMsgUntilMs = millis() + holdMs;
}

void setDoorMessage(const String& line1, const String& line2) {
  setDoorMessage(line1, line2, MSG_HOLD_MS);
}

void setDoorMessage(const String& line1) {
  setDoorMessage(line1, "", MSG_HOLD_MS);
}

bool isDoorMessageActive() {
  return doorMsg1.length() > 0 && (long)(doorMsgUntilMs - millis()) > 0;
}

void clearDoorMessageIfExpired() {
  if (doorMsg1.length() == 0) return;

  if ((long)(millis() - doorMsgUntilMs) >= 0) {
    doorMsg1 = "";
    doorMsg2 = "";
  }
}
