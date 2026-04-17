#ifndef DOOR_H
#define DOOR_H

/*
 * door.h — Door Lock Control & Feedback
 * Dieu khien khoa cua K01-12V va doc feedback trang thai.
 */

// ===== Door Lock State =====
bool doorUnlockActive = false;
unsigned long doorUnlockStartMs = 0;

void handleFeedbackDoorRelay(const char* id) {
  int relayPin = LOCK_RELAY_PIN;
  int relayState = digitalRead(relayPin);

  char payload[50];
  Serial.println(relayPin);

  if (relayState == HIGH) {
    sprintf(payload, "DOOR_%s_ON", id);
  }

  client.publish(TOPIC_STATUS, payload);
}

// ===== Mo khoa cua (kich hoat relay) =====
void triggerUnlockDoor() {
  digitalWrite(LOCK_RELAY_PIN, HIGH);
  doorUnlockActive = true;
  doorUnlockStartMs = millis();
  handleFeedbackDoorRelay("01");
}

// ===== Tu dong khoa lai sau thoi gian mo =====
void handleUnlockDoor() {
  if (!doorUnlockActive) return;
  if (millis() - doorUnlockStartMs >= DOOR_UNLOCK_MS) {
    digitalWrite(LOCK_RELAY_PIN, LOW);
    doorUnlockActive = false;
  }
}

// ===== Doc feedback khoa cua (log Serial) =====
// The debounce will protect against WDT reset and high CPU load if pins 34/35 are floating
void handleDoorFeedback() {
  static bool lastOpenFb  = HIGH;
  static bool lastCloseFb = HIGH;
  static unsigned long lastFbChangeMs = 0;

  bool openFb  = digitalRead(DOOR_FB_OPEN_PIN);
  bool closeFb = digitalRead(DOOR_FB_CLOSE_PIN);

  // Debounce 50ms to prevent floating pins from spamming the Serial TX buffer
  if ((openFb != lastOpenFb || closeFb != lastCloseFb) && (millis() - lastFbChangeMs > 50)) {
    lastFbChangeMs = millis();

    if (openFb != lastOpenFb) {
      Serial.print("[DOOR] Feedback OPEN: ");
      Serial.println(openFb == LOW ? "ACTIVE" : "INACTIVE");
      lastOpenFb = openFb;
    }

    if (closeFb != lastCloseFb) {
      Serial.print("[DOOR] Feedback CLOSE: ");
      Serial.println(closeFb == LOW ? "ACTIVE" : "INACTIVE");
      lastCloseFb = closeFb;
    }
  }
}

#endif
