void connectWiFi() {
  Serial.print("Connecting WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");
}

void subscribeTopics() {
  client.subscribe(TOPIC_LIGHT);
  client.subscribe(TOPIC_DOOR);
  client.subscribe(TOPIC_ALARM);
  client.subscribe(TOPIC_CURTAIN);
  client.subscribe(TOPIC_GET_DOOR_PASSWORD);
}

void connectMQTT() {
  if (client.connected()) return;
  if (millis() - mqttTryMs < MQTT_RETRY_MS) return;

  mqttTryMs = millis();
  Serial.print("Connecting MQTT...");

  if (client.connect(DEVICE_ID)) {
    Serial.println("Connected");
    subscribeTopics();
    setDoorMessage("MQTT Connected", "", 800);
  } else {
    Serial.print("Failed rc=");
    Serial.println(client.state());
  }
}

void onLight(const char* msg) {
  if (strcmp(msg, "ON_1") == 0) digitalWrite(LED_PIN_1, HIGH);
  else if (strcmp(msg, "OFF_1") == 0) digitalWrite(LED_PIN_1, LOW);
  else if (strcmp(msg, "ON_2") == 0) digitalWrite(LED_PIN_2, HIGH);
  else if (strcmp(msg, "OFF_2") == 0) digitalWrite(LED_PIN_2, LOW);
}

void onDoor(const char* msg) {
  if (strcmp(msg, "OPEN_1") == 0) Serial.println("Door Open command");
  else if (strcmp(msg, "CLOSE_1") == 0) Serial.println("Door Close command");
}

void onAlarm(const char* msg) {
  if (strcmp(msg, "ON_1") == 0) fireDetected = true;
  else if (strcmp(msg, "OFF_1") == 0) stopAlarm();
}

void onCurtain(const char* msg) {
  if (strcmp(msg, "OPEN_1") == 0) curtainOpenReq = true;
  else if (strcmp(msg, "CLOSE_1") == 0) curtainCloseReq = true;
}

void onDoorPassword(const char* msg) {
  String digits = "";
  for (uint8_t i = 0; msg[i] != '\0'; i++) {
    if (isDigit(msg[i])) {
      digits += msg[i];
      if (digits.length() >= PASS_LEN) break;
    }
  }

  if (digits.length() == PASS_LEN) {
    doorPassword = digits;
    if (!lockInput) {
      setDoorMessage("Password Synced", "", 900);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  char msg[96];
  unsigned int n = (length < sizeof(msg) - 1) ? length : (sizeof(msg) - 1);
  memcpy(msg, payload, n);
  msg[n] = '\0';

  if (strcmp(topic, TOPIC_LIGHT) == 0) onLight(msg);
  else if (strcmp(topic, TOPIC_DOOR) == 0) onDoor(msg);
  else if (strcmp(topic, TOPIC_ALARM) == 0) onAlarm(msg);
  else if (strcmp(topic, TOPIC_CURTAIN) == 0) onCurtain(msg);
  else if (strcmp(topic, TOPIC_GET_DOOR_PASSWORD) == 0) onDoorPassword(msg);
}
