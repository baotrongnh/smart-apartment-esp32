void submitDoorPassword() {
  if (inputPassword.length() != PASS_LEN) {
    setDoorMessage("Need 6 digits");
    return;
  }

  if (doorPassword.length() != PASS_LEN) {
    setDoorMessage("No server pass", "Wait syncing");
    return;
  }

  if (inputPassword == doorPassword) {
    client.publish(TOPIC_DOOR, "OPEN_1");
    inputPassword = "";
    wrongCount = 0;
    setDoorMessage("Door Open OK");
    return;
  }

  inputPassword = "";
  wrongCount++;

  int left = MAX_WRONG - wrongCount;
  if (left <= 0) {
    lockInput = true;
    lockUntilMs = millis() + LOCK_TIME_MS;
    setDoorMessage("Wrong Password", "Left:0");
  } else {
    setDoorMessage("Wrong Password", "Left:" + String(left));
  }
}

void handleDoorLock() {
  if (!lockInput) return;

  if ((long)(millis() - lockUntilMs) >= 0) {
    lockInput = false;
    wrongCount = 0;
    setDoorMessage("Unlocked", "Enter pass", 1000);
  }
}

void handleKeypadDoor() {
  char key = keypad.getKey();
  if (!key) return;

  onUserAction();

  if (lockInput) return;
  if (isDoorMessageActive()) return;

  if (key == '*') {
    inputPassword = "";
    return;
  }

  if (key == '#') {
    submitDoorPassword();
    return;
  }

  if (isDigit(key) && inputPassword.length() < PASS_LEN) {
    inputPassword += key;
  }
}

void updateDoorLCD() {
  if (!screensOn) return;

  if (lockInput) {
    long remain = (long)(lockUntilMs - millis());
    unsigned long remainSec = (remain <= 0) ? 0 : ((unsigned long)(remain + 999) / 1000);
    printLine(lcdDoor, 0, "Locked: " + String(remainSec) + "s");
    printLine(lcdDoor, 1, "Left:0");
    return;
  }

  if (isDoorMessageActive()) {
    printLine(lcdDoor, 0, doorMsg1);
    printLine(lcdDoor, 1, doorMsg2);
    return;
  }

  printLine(lcdDoor, 0, "Pass:" + stars(inputPassword.length()));
  printLine(lcdDoor, 1, "");
}

void requestDoorPassword() {
  if (doorPassword.length() == PASS_LEN) return;

  if (millis() - doorRequestTimer >= GET_PASS_INTERVAL_MS) {
    client.publish(TOPIC_STATUS, "GET_DOOR_PASSWORD");
    doorRequestTimer = millis();
  }
}
