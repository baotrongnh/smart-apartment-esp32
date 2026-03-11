#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

#include "config.h"

// ================= OBJECT =================
WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo doorServo;

// ================= PROJECT STATE =================
bool fireDetected = false;
bool firstFireState = true;
int displayMode = MODE_WATER;

// ================= TIME CONTROL =================
unsigned long alarmTimer = 0;
unsigned long flowTimer = 0;
unsigned long publishTimer = 0;

const int ALARM_INTERVAL = 200;

// ================= FLOW SENSOR =================
volatile int pulseCount = 0;
float waterFlowRate = 0;  // L/min
float waterTotal = 0;     // L

// ================= ELECTRIC (future PZEM) =================
float electricPower = 0;
float electricEnergy = 0;

// ================= INTERRUPT =================
void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

// ================= WIFI =================
void connectWiFi() {
  Serial.print("Connecting WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");

  lcd.clear();
  lcd.print("WiFi Connected");
  delay(1500);
}

// ================= MQTT =================
void connectMQTT() {
  if (client.connected()) return;
  Serial.print("Connecting MQTT...");

  if (client.connect(DEVICE_ID)) {

    Serial.println("Connected");

    lcd.clear();
    lcd.print("MQTT Connected");
    delay(1500);

    client.subscribe(TOPIC_LIGHT);
    client.subscribe(TOPIC_DOOR);
    client.subscribe(TOPIC_CMD);

  } else {
    Serial.println("Failed");
  }
}

// ================= MQTT CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length) {
  char msg[50];
  memcpy(msg, payload, length);
  msg[length] = '\0';

  Serial.println(msg);

  // ===== LIGHT =====
  if (strcmp(topic, TOPIC_LIGHT) == 0) {

    if (!strcmp(msg, "ON_1")) digitalWrite(LED_PIN_1, HIGH);
    if (!strcmp(msg, "OFF_1")) digitalWrite(LED_PIN_1, LOW);

    if (!strcmp(msg, "ON_2")) digitalWrite(LED_PIN_2, HIGH);
    if (!strcmp(msg, "OFF_2")) digitalWrite(LED_PIN_2, LOW);
  }

  // ===== DOOR =====
  if (strcmp(topic, TOPIC_DOOR) == 0) {

    if (!strcmp(msg, "DOOR_OPEN_1")) {
      Serial.println("Door Open");
    }

    if (!strcmp(msg, "DOOR_CLOSE_1")) {
      Serial.println("Door Close");
    }
  }

  // ===== ALARM CONTROL =====
  if (strcmp(topic, TOPIC_CMD) == 0) {
    if (!strcmp(msg, "ALARM_ON_1")) fireDetected = true;
    if (!strcmp(msg, "ALARM_OFF_1")) stopAlarm();
  }
}

// ================= STOP ALARM =================
void stopAlarm() {
  fireDetected = false;
  firstFireState = true;

  digitalWrite(LED_SOS_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  client.publish(TOPIC_STATUS, "FIRE_ACK");
}

// ================= BUTTON =================
void handleButton() {
  static bool lastState = HIGH;
  bool state = digitalRead(BUTTON_STOP_ALARM_PIN);

  if (lastState == HIGH && state == LOW) {
    stopAlarm();
  }

  lastState = state;
}

// ================= FLAME SENSOR =================
void handleFlameSensor() {
  if (digitalRead(FLAME_SENSOR) == LOW) {
    fireDetected = true;
  }
}

// ================= ALARM =================
void handleAlarm() {
  if (!fireDetected) return;

  if (millis() - alarmTimer < ALARM_INTERVAL) return;

  alarmTimer = millis();

  digitalWrite(LED_SOS_PIN, !digitalRead(LED_SOS_PIN));
  digitalWrite(BUZZER_PIN, !digitalRead(BUZZER_PIN));

  if (firstFireState) {
    client.publish(TOPIC_STATUS, "FIRE");
    firstFireState = false;
  }
}

// ================= FLOW SENSOR =================
void handleFlowSensor() {
  if (millis() - flowTimer < 1000) return;

  flowTimer = millis();

  noInterrupts();
  int count = pulseCount;
  pulseCount = 0;
  interrupts();

  /*
     Flow Sensor Formula

     pulses / 7.5 = L/min
  */

  waterFlowRate = count / 7.5;

  float litersPerSecond = waterFlowRate / 60.0;

  waterTotal += litersPerSecond;
}

void handleScreenButton() {

  static bool lastState = HIGH;
  bool state = digitalRead(BUTTON_SCREEN_PIN);

  if (lastState == HIGH && state == LOW) {

    displayMode = !displayMode;

    lcd.clear();

    Serial.println("Screen Changed");
  }

  lastState = state;
}

void updateLCD() {

  lcd.setCursor(0, 0);

  if (displayMode == MODE_WATER) {

    lcd.print("Flow:");
    lcd.print(waterFlowRate, 1);
    lcd.print("L/m ");

    lcd.setCursor(0, 1);
    lcd.print("Total:");
    lcd.print(waterTotal, 1);
    lcd.print("L ");

  }

  else if (displayMode == MODE_ELECTRIC) {

    lcd.print("Power:");
    lcd.print(electricPower, 1);
    lcd.print("W ");

    lcd.setCursor(0, 1);
    lcd.print("Energy:");
    lcd.print(electricEnergy, 2);
    lcd.print("kWh");
  }
}

// ================= PUBLISH USAGE =================
void publishUsage() {
  if (millis() - publishTimer < 10000) return;

  publishTimer = millis();

  char payload[120];

  sprintf(payload,
          "{\"waterTotal\":%.2f,\"waterFlow\":%.2f,\"electric\":%.2f}",
          waterTotal,
          waterFlowRate,
          electricEnergy);

  // client.publish(TOPIC_USAGE, payload);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN_1, OUTPUT);
  pinMode(LED_PIN_2, OUTPUT);
  pinMode(LED_SOS_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(FLAME_SENSOR, INPUT);
  pinMode(BUTTON_STOP_ALARM_PIN, INPUT);
  pinMode(FLOW_PIN, INPUT_PULLUP);
  pinMode(BUTTON_SCREEN_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), pulseCounter, FALLING);

  doorServo.setPeriodHertz(50);
  doorServo.attach(SERVO_PIN);
  doorServo.write(0);

  lcd.init();
  lcd.backlight();

  lcd.print("WELCOME HOME");
  delay(2000);
  lcd.clear();

  connectWiFi();

  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
}

// ================= LOOP =================
void loop() {
  connectMQTT();
  client.loop();

  handleFlameSensor();
  handleButton();
  handleAlarm();
  handleFlowSensor();
  handleScreenButton();
  updateLCD();

  publishUsage();
}