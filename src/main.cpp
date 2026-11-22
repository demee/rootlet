#include <Arduino.h>

namespace {
constexpr uint8_t SOIL_SENSOR_PIN = GPIO_NUM_4;
constexpr uint8_t READING_LED_PIN = GPIO_NUM_8;
constexpr uint32_t SERIAL_BAUD_RATE = 115200;
constexpr uint32_t SAMPLE_INTERVAL_MS = 1000;
constexpr uint32_t LED_FLASH_DURATION_MS = 50;
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  const uint32_t serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 2000) {
    delay(10);
  }
  analogReadResolution(12);
  pinMode(SOIL_SENSOR_PIN, INPUT);
  pinMode(READING_LED_PIN, OUTPUT);
  digitalWrite(READING_LED_PIN, LOW);

  Serial.println("Capacitive soil moisture sensor");
  Serial.println("AOUT -> GPIO4, VCC -> 3V3, GND -> GND");
}

void loop() {
  const int sensorReading = analogRead(SOIL_SENSOR_PIN);

  Serial.print("Raw ADC reading: ");
  Serial.println(sensorReading);

  digitalWrite(READING_LED_PIN, HIGH);
  delay(LED_FLASH_DURATION_MS);
  digitalWrite(READING_LED_PIN, LOW);
  delay(SAMPLE_INTERVAL_MS);
}