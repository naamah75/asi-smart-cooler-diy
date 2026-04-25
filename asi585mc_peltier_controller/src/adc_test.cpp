#include <Arduino.h>
#include <driver/adc.h>

constexpr uint8_t ADC_PINS[] = {32, 33, 34, 36, 39};

uint16_t readRawAverage(const uint8_t pin) {
  constexpr size_t sampleCount = 16;
  uint32_t sum = 0;
  for (size_t i = 0; i < sampleCount; ++i) {
    sum += analogRead(pin);
    delay(2);
  }
  return static_cast<uint16_t>(sum / sampleCount);
}

uint16_t readIdfAverage(const adc1_channel_t channel) {
  constexpr size_t sampleCount = 16;
  uint32_t sum = 0;
  for (size_t i = 0; i < sampleCount; ++i) {
    sum += adc1_get_raw(channel);
    delay(2);
  }
  return static_cast<uint16_t>(sum / sampleCount);
}

float rawToVoltage(const uint16_t raw) {
  return (static_cast<float>(raw) / 4095.0f) * 3.3f;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  analogReadResolution(12);
  adc1_config_width(ADC_WIDTH_BIT_12);

  for (const uint8_t pin : ADC_PINS) {
    pinMode(pin, INPUT);
    analogSetPinAttenuation(pin, ADC_11db);
  }

  adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);  // GPIO32
  adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_11);  // GPIO33
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);  // GPIO34
  adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);  // GPIO36 / VP
  adc1_config_channel_atten(ADC1_CHANNEL_3, ADC_ATTEN_DB_11);  // GPIO39 / VN

  Serial.println("ADC test minimale LOLIN D32 Pro");
  Serial.println("Collega un pin alla volta a 3.3V, poi a GND.");
  Serial.println("GPIO32=ADC1_CH4 GPIO33=ADC1_CH5 GPIO34=ADC1_CH6 GPIO36=ADC1_CH0 GPIO39=ADC1_CH3");
}

void loop() {
  const uint16_t raw32 = readRawAverage(32);
  const uint16_t raw33 = readRawAverage(33);
  const uint16_t raw34 = readRawAverage(34);
  const uint16_t raw36 = readRawAverage(36);
  const uint16_t raw39 = readRawAverage(39);
  const uint16_t idf36 = readIdfAverage(ADC1_CHANNEL_0);
  const uint16_t idf32 = readIdfAverage(ADC1_CHANNEL_4);
  const uint16_t idf33 = readIdfAverage(ADC1_CHANNEL_5);
  const uint16_t idf34 = readIdfAverage(ADC1_CHANNEL_6);
  const uint16_t idf39 = readIdfAverage(ADC1_CHANNEL_3);

  Serial.printf(
      "analogRead raw 32/33/34/36/39=%u/%u/%u/%u/%u V=%.3f/%.3f/%.3f/%.3f/%.3f | idf raw 32/33/34/36/39=%u/%u/%u/%u/%u V=%.3f/%.3f/%.3f/%.3f/%.3f\n",
      raw32,
      raw33,
      raw34,
      raw36,
      raw39,
      rawToVoltage(raw32),
      rawToVoltage(raw33),
      rawToVoltage(raw34),
      rawToVoltage(raw36),
      rawToVoltage(raw39),
      idf32,
      idf33,
      idf34,
      idf36,
      idf39,
      rawToVoltage(idf32),
      rawToVoltage(idf33),
      rawToVoltage(idf34),
      rawToVoltage(idf36),
      rawToVoltage(idf39));

  delay(500);
}
