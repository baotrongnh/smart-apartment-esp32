void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

void stopAlarm() {
  fireDetected = false;
  firstFireState = true;
  digitalWrite(LED_SOS_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  client.publish(TOPIC_STATUS, "FIRE_ACK");
}

void handleAlarm() {
  if (!fireDetected) return;
  if (millis() - alarmTimer < ALARM_INTERVAL_MS) return;

  alarmTimer = millis();
  digitalWrite(LED_SOS_PIN, !digitalRead(LED_SOS_PIN));
  digitalWrite(BUZZER_PIN, !digitalRead(BUZZER_PIN));

  if (firstFireState) {
    client.publish(TOPIC_STATUS, "FIRE");
    firstFireState = false;
  }
}

void handleFlameSensor() {
  if (digitalRead(FLAME_SENSOR) == LOW) {
    fireDetected = true;
  }
}

void handleStopAlarmButton() {
  static bool lastState = HIGH;
  bool state = digitalRead(BUTTON_STOP_ALARM_PIN);

  if (lastState == HIGH && state == LOW) {
    onUserAction();
    stopAlarm();
  }

  lastState = state;
}

void handleCurtain() {
  if (curtainOpenReq) {
    curtainServo.write(0);
    servoTimer = millis();
    servoRunning = true;
    curtainOpenReq = false;
  }

  if (curtainCloseReq) {
    curtainServo.write(180);
    servoTimer = millis();
    servoRunning = true;
    curtainCloseReq = false;
  }

  if (servoRunning && millis() - servoTimer >= SERVO_RUN_MS) {
    curtainServo.write(90);
    servoRunning = false;
  }
}

void handleFlowSensor() {
  if (millis() - flowTimer < FLOW_SAMPLE_MS) return;
  flowTimer = millis();

  noInterrupts();
  uint32_t count = pulseCount;
  pulseCount = 0;
  interrupts();

  waterFlowRate = count / 7.5f;
  waterTotal += waterFlowRate / 60.0f;
}

void handleScreenButton() {
  static bool lastState = HIGH;
  static unsigned long debounceMs = 0;

  bool state = digitalRead(BUTTON_SCREEN_PIN);
  if (lastState == HIGH && state == LOW && millis() - debounceMs > 200) {
    onUserAction();
    displayMode = (displayMode == MODE_WATER) ? MODE_ELECTRIC : MODE_WATER;
    debounceMs = millis();
  }

  lastState = state;
}

void updateMainLCD() {
  if (!screensOn) return;

  if (displayMode == MODE_WATER) {
    printLine(lcdMain, 0, "Flow:" + String(waterFlowRate, 1) + "L/m");
    printLine(lcdMain, 1, "Total:" + String(waterTotal, 1) + "L");
  } else {
    printLine(lcdMain, 0, "Power:" + String(electricPower, 1) + "W");
    printLine(lcdMain, 1, "Energy:" + String(electricEnergy, 2) + "kWh");
  }
}

void publishUsage() {
  if (millis() - usageTimer < USAGE_INTERVAL_MS) return;
  usageTimer = millis();

  char payload[120];
  snprintf(payload, sizeof(payload),
           "{\"waterTotal\":%.2f,\"waterFlow\":%.2f,\"electric\":%.2f}",
           waterTotal, waterFlowRate, electricEnergy);

  // client.publish(TOPIC_USAGE, payload);
}
