#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

Servo myServo;

// ===== WIFI =====
const char* ssid = "Bao Trong 5G";
const char* password = "29032004";

// ===== ID =====
const char* device_id = "ESP_A101";

// ===== MQTT =====
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* topic_light = "ESP_A101/light";
const char* topic_cmd = "ESP_A101/cmd";
const char* topic_status = "INTELL/ESP_A101/status";
const char* topic_door = "ESP_A101/door";

// ===== GPIO =====
#define LED_PIN_1 18
#define LED_PIN_2 19
#define FLAME_SENSOR 27
#define LED_SOS_PIN 26
#define BUZZER_PIN 25
#define FLOW_PIN 32
#define BUTTON_PIN 33
#define SERVO_PIN 23

WiFiClient espClient;
PubSubClient client(espClient);

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===== STATE =====
bool alarmTestMode = false;
bool fireDetected = false;
bool firstFireState = true;


volatile int pulseCount = 0;
float flowRate = 0.0;

unsigned long previousMillis = 0;
const long interval = 200;

unsigned long lastFlowMillis = 0;
float totalLiters = 0;

void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

void handleFlowSensor() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastFlowMillis >= 1000) {  // mỗi 1 giây
    lastFlowMillis = currentMillis;

    noInterrupts();  // tránh lỗi đọc biến volatile
    int count = pulseCount;
    pulseCount = 0;
    interrupts();

    flowRate = count / 7.5;  // Lít / phút

    float litersThisSecond = flowRate / 60.0;
    totalLiters += litersThisSecond;

    // Serial.print("Flow rate (L/min): ");
    // Serial.print(flowRate);
    // Serial.print(" | Total (L): ");
    // Serial.println(totalLiters);

    lcd.setCursor(0, 0);
    lcd.print("Flow:");
    lcd.print(flowRate, 2);
    lcd.print(" L/m   ");  // khoảng trắng để xóa số cũ

    lcd.setCursor(0, 1);
    lcd.print("Total:");
    lcd.print(totalLiters, 2);
    lcd.print(" L    ");
  }
}

// ================= WIFI =================
void connectWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
}

// ================= MQTT =================
void connectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting MQTT...");

    if (client.connect(device_id)) {
      Serial.println("connected");

      client.subscribe(topic_light);
      client.subscribe(topic_door);
      client.subscribe(topic_cmd);
    } else {
      Serial.println("failed");
      delay(1000);
    }
  }
}

// ================= MQTT CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";

  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Received: ");
  Serial.println(message);

  String topicStr = String(topic);
  Serial.println(topicStr);

  if (topicStr == topic_light) {
    if (message == "ON_1") {
      digitalWrite(LED_PIN_1, HIGH);
    } else if (message == "OFF_1") {
      digitalWrite(LED_PIN_1, LOW);
    }
    if (message == "ON_2") {
      digitalWrite(LED_PIN_2, HIGH);
    } else if (message == "OFF_2") {
      digitalWrite(LED_PIN_2, LOW);
    }
  } else if (topicStr == topic_cmd) {
    if (message == "ALARM_ON_1") {
      alarmTestMode = true;
    } else if (message == "ALARM_OFF_1") {
      firstFireState = true;
      alarmTestMode = false;
      digitalWrite(LED_SOS_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);
    }
  } else if (topicStr == topic_door) {
    Serial.println("Topic ok");
    if (message == "DOOR_OPEN_1") {
      Serial.println("DOOR_OPEN");
      myServo.write(90);
    } else if (message == "DOOR_CLOSE_1") {
      myServo.write(0);
    }
  }
}
// ================= FLAME SENSOR =================
void handleFlameSensor() {
  int flameState = digitalRead(FLAME_SENSOR);

  if (flameState == LOW) {
    fireDetected = true;
  }
}

void handleButton() {
  static bool lastState = HIGH;
  bool currentState = digitalRead(BUTTON_PIN);

  // phát hiện nhấn nút (cạnh xuống)
  if (lastState == HIGH && currentState == LOW) {
    Serial.println("Button pressed - STOP ALARM");

    fireDetected = false;
    alarmTestMode = false;
    firstFireState = true;

    digitalWrite(LED_SOS_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);

    client.publish(topic_status, "FIRE_ACK");
  }

  lastState = currentState;
}

// ================= ALARM CONTROL =================
void handleAlarm() {
  if (fireDetected || alarmTestMode) {
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;

      digitalWrite(LED_SOS_PIN, !digitalRead(LED_SOS_PIN));
      digitalWrite(BUZZER_PIN, !digitalRead(BUZZER_PIN));

      if (firstFireState) {
        Serial.print("SEND to server");
        client.publish(topic_status, "FIRE");
        firstFireState = false;
      }
    }
  } else {
    digitalWrite(LED_SOS_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN_1, OUTPUT);
  pinMode(LED_PIN_2, OUTPUT);
  pinMode(FLAME_SENSOR, INPUT);
  pinMode(LED_SOS_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(FLOW_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), pulseCounter, FALLING);

  myServo.setPeriodHertz(50);            // servo 50Hz
  myServo.attach(SERVO_PIN, 500, 2400);  // min/max pulse
  myServo.write(0);
  Serial.println("Servo test start");

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Water Monitor");
  delay(2000);
  lcd.clear();

  connectWiFi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

// ================= LOOP =================
void loop() {
  if (!client.connected()) {
    connectMQTT();
  }

  client.loop();

  handleFlameSensor();
  handleButton();
  handleAlarm();
  handleFlowSensor();
}