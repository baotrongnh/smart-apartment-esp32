#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <Keypad.h>
#include <Keypad_I2C.h>
#include "config.h"

// ---------- Config ----------
const byte I2C_ADDR = 0x20;
const byte ROWS = 4;
const byte COLS = 3;

const uint8_t LCD_COLS = 16;
//DOOR PASSWORD CONFIG:
const uint8_t PASS_LEN = 6;
const uint8_t MAX_WRONG = 5;

const unsigned long SCREEN_TIMEOUT_MS = 60000UL;
const unsigned long MQTT_RETRY_MS = 2000UL;
const unsigned long LOCK_TIME_MS = 30000UL;
const unsigned long MSG_HOLD_MS = 1200UL;
const unsigned long FLOW_SAMPLE_MS = 1000UL;
const unsigned long ALARM_INTERVAL_MS = 200UL;
const unsigned long SERVO_RUN_MS = 2000UL;
const unsigned long GET_PASS_INTERVAL_MS = 5000UL;
const unsigned long USAGE_INTERVAL_MS = 10000UL;

// ---------- Keypad ----------
char keys[ROWS][COLS] = {
  { '1', '2', '3' },
  { '4', '5', '6' },
  { '7', '8', '9' },
  { '*', '0', '#' }
};

byte rowPins[ROWS] = { 0, 1, 2, 3 };
byte colPins[COLS] = { 4, 5, 6 };
Keypad_I2C keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS, I2C_ADDR);

// ---------- Devices ----------
WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcdMain(0x26, 16, 2);
LiquidCrystal_I2C lcdDoor(0x27, 16, 2);
Servo curtainServo;

// ---------- State ----------
bool fireDetected = false;
bool firstFireState = true;
int displayMode = MODE_WATER;

volatile uint32_t pulseCount = 0;
float waterFlowRate = 0.0f;
float waterTotal = 0.0f;
float electricPower = 0.0f;
float electricEnergy = 0.0f;

bool curtainOpenReq = false;
bool curtainCloseReq = false;
bool servoRunning = false;

String doorPassword = "";
String inputPassword = "";
uint8_t wrongCount = 0;
bool lockInput = false;
unsigned long lockUntilMs = 0;

String doorMsg1 = "";
String doorMsg2 = "";
unsigned long doorMsgUntilMs = 0;

bool screensOn = false;
unsigned long lastUserActionMs = 0;

// ---------- Timers ----------
unsigned long mqttTryMs = 0;
unsigned long alarmTimer = 0;
unsigned long flowTimer = 0;
unsigned long servoTimer = 0;
unsigned long doorRequestTimer = 0;
unsigned long usageTimer = 0;

// Forward declarations for functions implemented in other .ino tabs.
void IRAM_ATTR pulseCounter();
void callback(char* topic, byte* payload, unsigned int length);

void setup() {
  Serial.begin(115200);
  Wire.begin();
  keypad.begin();

  pinMode(LED_PIN_1, OUTPUT);
  pinMode(LED_PIN_2, OUTPUT);
  pinMode(LED_SOS_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(FLAME_SENSOR, INPUT);
  pinMode(BUTTON_STOP_ALARM_PIN, INPUT_PULLUP);
  pinMode(FLOW_PIN, INPUT_PULLUP);
  pinMode(BUTTON_SCREEN_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), pulseCounter, FALLING);

  curtainServo.setPeriodHertz(50);
  curtainServo.attach(SERVO_PIN);
  curtainServo.write(90);

  lcdMain.init();
  lcdDoor.init();
  setScreens(false);

  connectWiFi();
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
}

void loop() {
  connectMQTT();
  client.loop();

  clearDoorMessageIfExpired();

  // User input first so screen wakes immediately.
  handleKeypadDoor();
  handleStopAlarmButton();
  handleScreenButton();

  handleDoorLock();
  requestDoorPassword();
  handleCurtain();
  handleFlameSensor();
  handleAlarm();
  handleFlowSensor();

  updateDoorLCD();
  updateMainLCD();

  publishUsage();
  handleScreenTimeout();
}
