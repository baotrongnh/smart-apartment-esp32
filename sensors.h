#ifndef SENSORS_H
#define SENSORS_H

/*
 * sensors.h — Water Flow Sensor & PZEM004T Power Meter
 *
 * Reads water flow rate (L/min) and electrical data (current, energy).
 * Flow sensor uses hardware interrupt for accurate pulse counting.
 * PZEM004T communicates via UART2 (Serial2).
 */

// ==================== Flow Sensor State ====================

volatile int pulseCount = 0;   // Incremented by ISR (interrupt)
float flowRate    = 0.0f;      // Current flow rate (L/min)
float totalLiters = 0.0f;      // Cumulative water usage (L)

// ====================== PZEM State =========================

float current = NAN;  // Electrical current (A), NAN = read error
float energy  = NAN;  // Energy consumption (kWh), NAN = read error

// ======================= Timing ============================

unsigned long lastFlowMs     = 0;
unsigned long lastPzemReadMs = 0;

// ============ ISR: Flow sensor pulse counter ===============
// IRAM_ATTR ensures this runs from internal RAM (required for ESP32 ISR)

void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

// ========= Flow sensor: calculate flow every 1 second =======

void handleFlowSensor() {
  unsigned long now = millis();
  if (now - lastFlowMs < FLOW_INTERVAL_MS) return;
  lastFlowMs = now;

  // Safely read and reset pulse count (disable interrupts briefly)
  noInterrupts();
  int count = pulseCount;
  pulseCount = 0;
  interrupts();

  // Calculate flow rate and accumulate total
  flowRate = count / PULSE_FREQUENCY;
  totalLiters += flowRate / PULSE_FREQUENCY;
}

// ======= PZEM: read current & energy periodically ==========

void handlePzemPoll() {
  unsigned long now = millis();
  if (now - lastPzemReadMs < PZEM_READ_INTERVAL_MS) return;
  lastPzemReadMs = now;

  current = pzem.current();
  energy  = pzem.energy();

  if (isnan(current) || isnan(energy)) {
    Serial.println("[PZEM] Read error (NaN)");
  }
}

#endif
