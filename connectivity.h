#include "Client.h"
#ifndef CONNECTIVITY_H
#define CONNECTIVITY_H

/*
 * connectivity.h — WiFi & MQTT
 * Ket noi WiFi, MQTT, xử lý message đến, đổi mật khẩu qua MQTT.
 */

// ===== Connection State =====
unsigned long lastMqttRetryMs = 0;
unsigned long connectedAt = 0;
bool apScheduledToClose = false;
bool provisioningApActive = false;
bool mqttFirstConnect = true;   // flag: chua connect MQTT lan nao
bool passwordReceived = false;  // flag: da nhan password tu server
bool wifiCredentialsSaved = false;

// ===== WiFi Config State (shared with web_api.h) =====
String wifiSSID = "";
String wifiPASS = "";
String connectionStatus = "idle";  // idle | connecting | connected | failed
unsigned long connectStartTime = 0;

void startProvisioningAP() {
  if (provisioningApActive) return;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  provisioningApActive = true;
  Serial.print("[WIFI] AP started, IP: ");
  Serial.println(WiFi.softAPIP());
}

// ===== LCD Status Helpers =====
void lcdShowStatus(const char* line0, const char* line1) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line0);
  lcd.setCursor(0, 1);
  lcd.print(line1);
}

// ===== Kết nối & giữ MQTT =====
void serviceMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (client.connected()) return;

  unsigned long now = millis();
  if (now - lastMqttRetryMs < MQTT_RETRY_MS) return;
  lastMqttRetryMs = now;

  Serial.print("[MQTT] Connecting... ");
  lcdShowStatus("MQTT", "Connecting...");

  if (client.connect(DEVICE_ID)) {
    Serial.println("OK");
    lcdShowStatus("MQTT", "Connected!");

    client.subscribe(TOPIC_LIGHT);
    client.subscribe(TOPIC_DOOR);
    client.subscribe(TOPIC_ALARM);
    client.subscribe(TOPIC_STATUS);
    client.subscribe(TOPIC_GET_DOOR_PASSWORD);
    client.subscribe(TOPIC_CURTAIN);
    client.subscribe(TOPIC_GET_TELEMETRY);

    // Moi lan ket noi MQTT -> lay mat khau moi nhat
    client.publish(TOPIC_STATUS, "GET_DOOR_PASSWORD");
    Serial.println("[MQTT] Published GET_DOOR_PASSWORD");

    if (mqttFirstConnect) {
      lcdShowStatus("Password", "Requesting...");
      mqttFirstConnect = false;
    }
  } else {
    Serial.println("FAILED");
    lcdShowStatus("MQTT", "Failed!");
  }
}

void handleFeedbackLightRelay(const char* id) {
  int relayPin = (strcmp(id, "02") == 0) ? LED_PIN_2 : LED_PIN_1;
  int relayState = digitalRead(relayPin);

  char payload[50];

  if (relayState == HIGH) {
    sprintf(payload, "LIGHT_%s_ON", id);
  } else {
    sprintf(payload, "LIGHT_%s_OFF", id);
  }

  client.publish(TOPIC_STATUS, payload);
}

// ===== MQTT Callback: xử lý khi có message đến =====
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.printf("[MQTT] %s -> %s\n", topic, message.c_str());

  // --- Control light ---
  if (strcmp(topic, TOPIC_LIGHT) == 0) {
    if (message == "ON_1") {
      digitalWrite(LED_PIN_1, HIGH);
      handleFeedbackLightRelay("01");
    } else if (message == "OFF_1") {
      digitalWrite(LED_PIN_1, LOW);
      handleFeedbackLightRelay("01");
    } else if (message == "ON_2") {
      digitalWrite(LED_PIN_2, HIGH);
      handleFeedbackLightRelay("02");
    } else if (message == "OFF_2") {
      digitalWrite(LED_PIN_2, LOW);
      handleFeedbackLightRelay("02");
    }
    return;
  }

  if (strcmp(topic, TOPIC_GET_TELEMETRY) == 0) {
    StaticJsonDocument<128> doc;
    doc["water_total"] = totalLiters;
    doc["energy_total"] = isnan(energy) ? 0.0f : energy;

    char telemetryPayload[128];
    serializeJson(doc, telemetryPayload, sizeof(telemetryPayload));
    client.publish(TOPIC_SEND_TELEMETRY, telemetryPayload);
    return;
  }

  // --- Control door ---
  if (strcmp(topic, TOPIC_DOOR) == 0) {
    if (message == "ON_1") triggerUnlockDoor();
    return;
  }

  // --- Fire alarm test ---
  if (strcmp(topic, TOPIC_ALARM) == 0) {
    if (message == "ON_1") {
      fireDetected = true;
    } else if (message == "OFF_1") {
      fireDetected = false;
      firstFireState = true;
      stopAlarmOutputs();
    }
    return;
  }

  // --- Control curtain ---
  if (strcmp(topic, TOPIC_CURTAIN) == 0) {
    if (message == "ON_1") {
      curtainCmdClose = false;
      curtainCmdOpen = true;
    } else if (message == "OFF_1") {
      curtainCmdOpen = false;
      curtainCmdClose = true;
    }
    return;
  }

  if (strcmp(topic, TOPIC_STATUS) == 0) {
    if (message == "ARE_YOU_OK") {
      client.publish(TOPIC_STATUS, "ONLINE");
    }
    return;
  }

  // --- Change door password ---
  if (strcmp(topic, TOPIC_GET_DOOR_PASSWORD) == 0) {
    if (message.length() == PASSWORD_LEN) {
      // Dat mat khau moi
      strncpy(doorPassword, message.c_str(), PASSWORD_LEN);
      doorPassword[PASSWORD_LEN] = '\0';
      Serial.printf("[DOOR] Password updated: %s\n", doorPassword);
      client.publish(TOPIC_STATUS, "PWD_UPDATED");

      // Hien thi trang thai nhan password len LCD
      if (!passwordReceived) {
        passwordReceived = true;
        lcdShowStatus("Password", "Received OK!");
        Serial.println("[LCD] Password received, showing status");
      }
    } else {
      Serial.printf("[DOOR] Invalid password length: %d\n", message.length());
    }
    return;
  }
}

// ===== Xu ly WiFi async (tu web config) =====
void handleWiFiConnection() {
  static bool wifiStatusShown = false;

  if (connectionStatus == "connecting") {
    if (!wifiStatusShown) {
      lcdShowStatus("WiFi", "Connecting...");
      wifiStatusShown = true;
    }
    if (WiFi.status() == WL_CONNECTED) {
      connectionStatus = "connected";
      connectedAt = millis();
      apScheduledToClose = provisioningApActive;

      if (!wifiCredentialsSaved && wifiSSID.length() > 0) {
        prefsWifi.putString("ssid", wifiSSID);
        prefsWifi.putString("pass", wifiPASS);
        wifiCredentialsSaved = true;
        Serial.println("[WIFI] Credentials saved to NVS");
      }

      wifiStatusShown = false;
      lcdShowStatus("WiFi", "Connected!");
      Serial.print("[WIFI] Connected! IP: ");
      Serial.println(WiFi.localIP());
      return;
    }

    if (millis() - connectStartTime > WIFI_CONNECT_TIMEOUT_MS) {
      connectionStatus = "failed";
      wifiStatusShown = false;
      lcdShowStatus("WiFi", "Start AP mode");
      Serial.println("[WIFI] Connect timeout, fallback AP mode");
      startProvisioningAP();
    }
  }

  // Tat AP sau 5s khi da ket noi WiFi Station
  if (apScheduledToClose && millis() - connectedAt > AP_SHUTDOWN_DELAY_MS) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apScheduledToClose = false;
    provisioningApActive = false;
    Serial.println("[WIFI] AP closed, switched to STA mode");
  }
}

#endif
