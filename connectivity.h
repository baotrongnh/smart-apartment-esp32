#include "HardwareSerial.h"
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
bool wifiCredentialsSaved = false;
unsigned long lastTelemetryPublishMs = 0;
bool startupStatusPublished = false;

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

void handleFeedbackLightRelay(const char* id);
void publishStartupStatusOnce();

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
    publishStartupStatusOnce();
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

void publishStartupStatusOnce() {
  if (startupStatusPublished) return;

  handleFeedbackLightRelay("01");
  handleFeedbackLightRelay("02");

  bool openLimitActive = (digitalRead(LIMIT_SWITCH_OPEN_PIN) == LOW);
  bool closeLimitActive = (digitalRead(LIMIT_SWITCH_CLOSE_PIN) == LOW);

  if (openLimitActive) {
    client.publish(TOPIC_STATUS, "CURTAIN_01_ON");
  } else if (closeLimitActive) {
    client.publish(TOPIC_STATUS, "CURTAIN_01_OFF");
  }

  startupStatusPublished = true;
}

void publishTelemetryNow() {
  StaticJsonDocument<128> doc;
  doc["water_total"] = totalLiters;
  doc["energy_total"] = isnan(energy) ? 0.0f : energy;

  char telemetryPayload[128];
  serializeJson(doc, telemetryPayload, sizeof(telemetryPayload));
  client.publish(TOPIC_SEND_TELEMETRY, telemetryPayload);
}

void handleTelemetryPublish() {
  if (!client.connected()) return;

  unsigned long now = millis();
  if (now - lastTelemetryPublishMs < TELEMETRY_PUBLISH_INTERVAL_MS) return;
  lastTelemetryPublishMs = now;

  publishTelemetryNow();
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
    publishTelemetryNow();
    return;
  }

  // --- Control door ---
  if (strcmp(topic, TOPIC_DOOR) == 0) {
    if (message == "ON_1") {
      triggerUnlockDoor();
    }
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
      // Chi cap nhat khi server gui password moi khac password hien tai
      if (setDoorPassword(message.c_str(), true)) {
        Serial.printf("[DOOR] Password updated: %s\n", doorPassword);
        client.publish(TOPIC_STATUS, "PWD_UPDATED");
        lcdShowStatus("Password", "Updated!");
        Serial.println("[LCD] Password changed from server, saved to NVS");
      } else {
        Serial.println("[DOOR] Password unchanged, ignore");
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
