#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_ADS1X15.h>
#ifndef USE_SHT30
#define USE_SHT30 1
#endif
#if USE_SHT30
#include <Adafruit_SHT31.h>
#else
#include <Adafruit_BME280.h>
#endif
#include <Adafruit_INA219.h>
#if BOARD_LOLIN_S2_PICO
#include <Adafruit_SSD1306.h>
#endif
#include <DNSServer.h>
#ifndef ENABLE_BLE
#define ENABLE_BLE 1
#endif

#if ENABLE_BLE
#include <NimBLEDevice.h>
#endif
#include <Preferences.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <driver/adc.h>
#include <time.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef USE_ADS1115
#define USE_ADS1115 0
#endif

#ifndef USE_ACS71X_CURRENT
#define USE_ACS71X_CURRENT 0
#endif

#ifndef ACS_CURRENT_SENS_MV_PER_A
#define ACS_CURRENT_SENS_MV_PER_A 90.0f
#endif

#ifndef ACS_ZERO_V
#define ACS_ZERO_V 1.65f
#endif

#ifndef INA219_SHUNT_RESISTOR_OHM
#define INA219_SHUNT_RESISTOR_OHM 0.1f
#endif

#ifndef THEME_RETRO_AMBER
#define THEME_RETRO_AMBER 1
#endif

#ifndef NTC_ADC_FULL_SCALE_V
#if CONFIG_IDF_TARGET_ESP32S2
#define NTC_ADC_FULL_SCALE_V 2.5f
#else
#define NTC_ADC_FULL_SCALE_V 3.3f
#endif
#endif

#ifndef NTC_SERIES_RESISTOR_OHM
#define NTC_SERIES_RESISTOR_OHM 10000.0f
#endif

#ifndef NTC_SUPPLY_VOLTAGE
#define NTC_SUPPLY_VOLTAGE 3.3f
#endif

#ifndef NTC_COLD_TEMP_OFFSET_C
#define NTC_COLD_TEMP_OFFSET_C 0.0f
#endif

#ifndef NTC_HOT_TEMP_OFFSET_C
#define NTC_HOT_TEMP_OFFSET_C 0.0f
#endif

#ifndef NTC_ADC_SAMPLE_COUNT
#define NTC_ADC_SAMPLE_COUNT 64
#endif

#ifndef NTC_TEMP_FILTER_ALPHA
#define NTC_TEMP_FILTER_ALPHA 0.15f
#endif

namespace pins {
#if BOARD_LOLIN_S2_PICO
constexpr uint8_t PELTIER_PWM = 11;
constexpr uint8_t STATUS_LED = 10;
constexpr uint8_t COLD_NTC_ADC = 1;
constexpr uint8_t HOT_NTC_ADC = 2;
constexpr uint8_t CURRENT_ADC = 3;
constexpr uint8_t I2C_SDA = 8;
constexpr uint8_t I2C_SCL = 9;
constexpr uint8_t OLED_RESET = 18;
#else
constexpr uint8_t PELTIER_PWM = 25;
constexpr uint8_t STATUS_LED = 5;
constexpr uint8_t COLD_NTC_ADC = 33;
constexpr uint8_t HOT_NTC_ADC = 36;
constexpr uint8_t CURRENT_ADC = 32;
constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;
constexpr uint8_t OLED_RESET = 255;
#endif
}  // namespace pins

namespace ads_channels {
constexpr uint8_t COLD_NTC = 0;
constexpr uint8_t HOT_NTC = 1;
constexpr uint8_t CURRENT = 2;
}  // namespace ads_channels

namespace pwm {
constexpr uint8_t CHANNEL = 0;
constexpr uint16_t FREQUENCY_HZ = 20000;
constexpr uint8_t RESOLUTION_BITS = 10;
constexpr uint16_t MAX_DUTY = (1U << RESOLUTION_BITS) - 1U;
}  // namespace pwm

namespace timing {
constexpr uint32_t SENSOR_MS = 1000;
constexpr uint32_t CONTROL_MS = 1000;
constexpr uint32_t WIFI_RETRY_MS = 15000;
constexpr uint32_t STATUS_NOTIFY_MS = 2000;
constexpr uint32_t SERIAL_LOG_MS = 5000;
}  // namespace timing

struct Calibration {
  float coldOffsetC = NTC_COLD_TEMP_OFFSET_C;
  float hotOffsetC = NTC_HOT_TEMP_OFFSET_C;
  float ambientTempOffsetC = 0.0f;
  float humidityOffsetPct = 0.0f;
  float currentOffsetA = 0.0f;
  float currentScale = 1.0f;
};

struct PidConfig {
  float kp = 20.0f;
  float ki = 0.25f;
  float kd = 12.0f;
};

struct ControlConfig {
  bool enabled = false;
  bool hotSensorEnabled = false;
  float targetC = 5.0f;
  float dewMarginC = 2.0f;
  float maxDuty = 1.0f;
  float maxHotSideC = 55.0f;
};

struct ThermistorConfig {
  float seriesResistorOhm = NTC_SERIES_RESISTOR_OHM;
  float nominalResistanceOhm = 10000.0f;
  float nominalTempC = 25.0f;
  float beta = 3950.0f;
  float supplyVoltage = NTC_SUPPLY_VOLTAGE;
};

struct CurrentSensorConfig {
  float zeroVoltageV = ACS_ZERO_V;
  float sensitivityMvPerA = ACS_CURRENT_SENS_MV_PER_A;
};

struct PresetConfig {
  float targetC = 5.0f;
  float dewMarginC = 2.0f;
  float maxDuty = 1.0f;
  float maxHotSideC = 55.0f;
  bool hotSensorEnabled = false;
};

struct NetworkConfig {
  String wifiSsid;
  String wifiPassword;
  String hostname = "asi585mc-cooler";
  bool dhcpEnabled = true;
  IPAddress staticIp{10, 0, 0, 50};
  IPAddress gateway{10, 0, 0, 1};
  IPAddress subnet{255, 255, 255, 0};
  IPAddress dns1{1, 1, 1, 1};
};

struct SensorState {
  float coldC = NAN;
  float hotC = NAN;
  float coldRawC = NAN;
  float hotRawC = NAN;
  float ambientC = NAN;
  float humidityPct = NAN;
  float dewPointC = NAN;
  float currentA = NAN;
  float busVoltageV = NAN;
  float shuntVoltageMv = NAN;
  float coldNtcVoltageV = NAN;
  float hotNtcVoltageV = NAN;
  float coldNtcResistanceOhm = NAN;
  float hotNtcResistanceOhm = NAN;
  uint16_t coldNtcRaw = 0;
  uint16_t hotNtcRaw = 0;
  uint16_t adc32Raw = 0;
  uint16_t adc33Raw = 0;
  uint16_t adc34Raw = 0;
  uint16_t adc32IdfRaw = 0;
  uint16_t adc33IdfRaw = 0;
  uint16_t adc34IdfRaw = 0;
  float adc32VoltageV = NAN;
  float adc33VoltageV = NAN;
  float adc34VoltageV = NAN;
  bool ambientSensorOk = false;
  bool bmeOk = false;
  bool inaOk = false;
  bool adsOk = false;
  bool currentOk = false;
  bool coldNtcOk = false;
  bool hotNtcOk = false;
};

struct ControlState {
  float effectiveTargetC = NAN;
  float pwmDuty = 0.0f;
  float pidOutput = 0.0f;
  bool dewClampActive = false;
  bool hotProtectionActive = false;
  bool pidBaseTuned = false;
  String pidProfile = "manual";
};

struct DebugState {
  bool manualPwmEnabled = false;
  float manualPwmDuty = 0.0f;
  float coldMinC = NAN;
  float coldMaxC = NAN;
  float hotMinC = NAN;
  float hotMaxC = NAN;
  float coldTrendCPerMin = NAN;
  float hotTrendCPerMin = NAN;
  float peltierTrendPowerPct = NAN;
  uint32_t lastTrendMs = 0;
  float lastColdTrendC = NAN;
  float lastHotTrendC = NAN;
};

Calibration calibration;
PidConfig pidConfig;
ControlConfig controlConfig;
ThermistorConfig thermistorConfig;
CurrentSensorConfig currentSensorConfig;
NetworkConfig networkConfig;
PresetConfig presetEstate;
PresetConfig presetInverno;
PresetConfig presetDeepCooling;
SensorState sensorState;
ControlState controlState;
DebugState debugState;

Preferences preferences;
#if USE_SHT30
Adafruit_SHT31 ambientSensor = Adafruit_SHT31();
#else
Adafruit_BME280 bme;
#endif
Adafruit_INA219 ina219;
Adafruit_ADS1115 ads;
#if BOARD_LOLIN_S2_PICO
Adafruit_SSD1306 display(128, 32, &Wire, pins::OLED_RESET);
#endif
WebServer server(80);
DNSServer dnsServer;
#if ENABLE_BLE
NimBLEServer* bleServer = nullptr;
NimBLECharacteristic* bleStatusCharacteristic = nullptr;
NimBLECharacteristic* bleCommandCharacteristic = nullptr;
NimBLECharacteristic* bleTelemetryCharacteristic = nullptr;
NimBLECharacteristic* bleEnabledCharacteristic = nullptr;
NimBLECharacteristic* bleTargetCharacteristic = nullptr;
NimBLECharacteristic* blePresetCharacteristic = nullptr;
NimBLECharacteristic* bleTuneCharacteristic = nullptr;
#endif

uint32_t lastSensorMs = 0;
uint32_t lastControlMs = 0;
uint32_t lastWifiAttemptMs = 0;
uint32_t lastBleNotifyMs = 0;
uint32_t lastSerialLogMs = 0;
uint32_t lastLedToggleMs = 0;
float pidIntegral = 0.0f;
float pidLastError = 0.0f;
bool pidHasHistory = false;
bool mdnsStarted = false;
bool captivePortalActive = false;
bool ntpConfigured = false;
bool ambientSensorPresent = false;
bool ina219Present = false;
bool wifiConnectedLogged = false;
bool statusLedOn = false;
uint8_t i2cDeviceCount = 0;
String i2cScanSummary = "non eseguita";
time_t bootEpoch = 0;

void writeStatusLed(const bool on) {
  statusLedOn = on;
  digitalWrite(pins::STATUS_LED, on ? LOW : HIGH);
}

void resetDebugTemperatureMinMax() {
  debugState.coldMinC = NAN;
  debugState.coldMaxC = NAN;
  debugState.hotMinC = NAN;
  debugState.hotMaxC = NAN;
}

void updateDebugTemperatureMinMax() {
  if (isfinite(sensorState.coldC)) {
    debugState.coldMinC = isfinite(debugState.coldMinC) ? min(debugState.coldMinC, sensorState.coldC) : sensorState.coldC;
    debugState.coldMaxC = isfinite(debugState.coldMaxC) ? max(debugState.coldMaxC, sensorState.coldC) : sensorState.coldC;
  }
  if (isfinite(sensorState.hotC)) {
    debugState.hotMinC = isfinite(debugState.hotMinC) ? min(debugState.hotMinC, sensorState.hotC) : sensorState.hotC;
    debugState.hotMaxC = isfinite(debugState.hotMaxC) ? max(debugState.hotMaxC, sensorState.hotC) : sensorState.hotC;
  }
}

float filterTemperatureC(const float rawC, const float previousFilteredC) {
  if (!isfinite(rawC)) {
    return NAN;
  }
  if (!isfinite(previousFilteredC)) {
    return rawC;
  }
  return previousFilteredC + (NTC_TEMP_FILTER_ALPHA * (rawC - previousFilteredC));
}

void updateDebugTemperatureTrend() {
  const uint32_t now = millis();
  if (debugState.lastTrendMs == 0) {
    debugState.lastTrendMs = now;
    debugState.lastColdTrendC = sensorState.coldC;
    debugState.lastHotTrendC = sensorState.hotC;
    return;
  }

  const float dtMin = static_cast<float>(now - debugState.lastTrendMs) / 60000.0f;
  if (dtMin < 0.25f) {
    return;
  }

  if (isfinite(sensorState.coldC) && isfinite(debugState.lastColdTrendC)) {
    debugState.coldTrendCPerMin = (sensorState.coldC - debugState.lastColdTrendC) / dtMin;
  }
  if (isfinite(sensorState.hotC) && isfinite(debugState.lastHotTrendC)) {
    debugState.hotTrendCPerMin = (sensorState.hotC - debugState.lastHotTrendC) / dtMin;
  }

  debugState.peltierTrendPowerPct = isfinite(debugState.coldTrendCPerMin)
                                     ? min(max(-debugState.coldTrendCPerMin * 20.0f, 0.0f), 100.0f)
                                     : NAN;
  debugState.lastTrendMs = now;
  debugState.lastColdTrendC = sensorState.coldC;
  debugState.lastHotTrendC = sensorState.hotC;
}

constexpr char PREF_NAMESPACE[] = "cooler";
#if ENABLE_BLE
constexpr char BLE_SERVICE_UUID[] = "9b220100-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_STATUS_UUID[] = "9b220101-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_COMMAND_UUID[] = "9b220102-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_STRUCTURED_SERVICE_UUID[] = "9b220200-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_TELEMETRY_UUID[] = "9b220201-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_ENABLE_UUID[] = "9b220202-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_TARGET_UUID[] = "9b220203-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_PRESET_UUID[] = "9b220204-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_TUNE_UUID[] = "9b220205-35ac-4e4e-9f6f-8ed48fc5c001";
#endif
constexpr char FW_VERSION[] = "0.1.0-dev";
constexpr char PROJECT_URL[] = "https://github.com/naamah75/asi-smart-cooler-diy";

const char* const PRESET_ESTATE = "estate";
const char* const PRESET_INVERNO = "inverno";
const char* const PRESET_DEEP = "deep_cooling";
constexpr byte DNS_PORT = 53;
const IPAddress CAPTIVE_IP(192, 168, 4, 1);

void savePreferences();
void syncBleCharacteristics(const bool notify);
void ensureWifi();
void startCaptivePortal();
void stopCaptivePortal();
void reconnectWifi();

float clampf(const float value, const float minValue, const float maxValue) {
  return max(minValue, min(value, maxValue));
}

bool isPlaceholderSecret(const char* value) {
  return value == nullptr || strlen(value) == 0 || String(value).startsWith("YOUR_");
}

String defaultWifiSsid() {
  return isPlaceholderSecret(secrets::WIFI_SSID) ? String() : String(secrets::WIFI_SSID);
}

String defaultWifiPassword() {
  return isPlaceholderSecret(secrets::WIFI_PASSWORD) ? String() : String(secrets::WIFI_PASSWORD);
}

String defaultHostname() {
  return isPlaceholderSecret(secrets::HOSTNAME) ? String("asi585mc-cooler") : String(secrets::HOSTNAME);
}

bool parseIpAddressString(const String& value, IPAddress& output) {
  return output.fromString(value);
}

String ipAddressToString(const IPAddress& address) {
  return address.toString();
}

String formatLocalTimeString(const time_t epoch) {
  if (epoch <= 0) {
    return String();
  }

  struct tm timeInfo;
  if (localtime_r(&epoch, &timeInfo) == nullptr) {
    return String();
  }

  char buffer[24];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
  return String(buffer);
}

bool hasWifiCredentials() {
  return !networkConfig.wifiSsid.isEmpty();
}

void scanI2cBus() {
  Serial.println("Scan I2C iniziale...");
  uint8_t found = 0;
  String summary;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  I2C trovato: 0x%02X\n", address);
      if (!summary.isEmpty()) {
        summary += ", ";
      }
      char buffer[6];
      snprintf(buffer, sizeof(buffer), "0x%02X", address);
      summary += buffer;
      ++found;
    }
    delay(2);
  }
  i2cDeviceCount = found;
  i2cScanSummary = found > 0 ? summary : String("nessuna periferica rilevata");
  if (found == 0) {
    Serial.println("  Nessuna periferica I2C rilevata");
  }
}

float readEspAnalogVoltageV(const uint8_t pin) {
  constexpr size_t sampleCount = NTC_ADC_SAMPLE_COUNT;
#if CONFIG_IDF_TARGET_ESP32S2
  uint32_t mvSum = 0;

  for (size_t i = 0; i < sampleCount; ++i) {
    mvSum += analogReadMilliVolts(pin);
    delay(2);
  }

  return (static_cast<float>(mvSum) / static_cast<float>(sampleCount)) / 1000.0f;
#else
  uint32_t rawSum = 0;

  for (size_t i = 0; i < sampleCount; ++i) {
    rawSum += analogRead(pin);
    delay(2);
  }

  const float raw = static_cast<float>(rawSum) / static_cast<float>(sampleCount);
  return (raw / 4095.0f) * thermistorConfig.supplyVoltage;
#endif
}

float espRawToVoltageV(const uint16_t raw) {
  return (static_cast<float>(raw) / 4095.0f) * NTC_ADC_FULL_SCALE_V;
}

uint16_t readEspAnalogRaw(const uint8_t pin) {
  constexpr size_t sampleCount = NTC_ADC_SAMPLE_COUNT;
  uint32_t rawSum = 0;

  for (size_t i = 0; i < sampleCount; ++i) {
    rawSum += analogRead(pin);
    delay(2);
  }

  return static_cast<uint16_t>(roundf(static_cast<float>(rawSum) / static_cast<float>(sampleCount)));
}

uint16_t readAdc1Raw(const adc1_channel_t channel) {
  constexpr size_t sampleCount = 16;
  int rawSum = 0;

  for (size_t i = 0; i < sampleCount; ++i) {
    rawSum += adc1_get_raw(channel);
    delay(2);
  }

  return static_cast<uint16_t>(roundf(static_cast<float>(rawSum) / static_cast<float>(sampleCount)));
}

float readAdsAnalogVoltageV(const uint8_t channel) {
  constexpr size_t sampleCount = 8;
  float voltageSum = 0.0f;

  for (size_t i = 0; i < sampleCount; ++i) {
    const int16_t raw = ads.readADC_SingleEnded(channel);
    voltageSum += ads.computeVolts(raw);
    delay(2);
  }

  return voltageSum / static_cast<float>(sampleCount);
}

float readAnalogVoltageV(const uint8_t source) {
#if USE_ADS1115
  return sensorState.adsOk ? readAdsAnalogVoltageV(source) : NAN;
#else
  return readEspAnalogVoltageV(source);
#endif
}

float thermistorTemperatureFromVoltageC(const float voltage) {
  if (voltage <= 0.001f || voltage >= thermistorConfig.supplyVoltage - 0.001f) {
    return NAN;
  }

  const float resistance = thermistorConfig.seriesResistorOhm * voltage /
                           (thermistorConfig.supplyVoltage - voltage);
  if (resistance <= 0.0f) {
    return NAN;
  }

  const float nominalTempK = thermistorConfig.nominalTempC + 273.15f;
  const float steinhart = (1.0f / nominalTempK) +
                          (1.0f / thermistorConfig.beta) *
                              logf(resistance / thermistorConfig.nominalResistanceOhm);
  return (1.0f / steinhart) - 273.15f;
}

float thermistorResistanceFromVoltageOhm(const float voltage) {
  if (voltage <= 0.001f || voltage >= thermistorConfig.supplyVoltage - 0.001f) {
    return NAN;
  }
  return thermistorConfig.seriesResistorOhm * voltage /
         (thermistorConfig.supplyVoltage - voltage);
}

float readThermistorTemperatureC(const uint8_t source) {
  return thermistorTemperatureFromVoltageC(readAnalogVoltageV(source));
}

float computeDewPointC(const float temperatureC, const float humidityPct) {
  if (isnan(temperatureC) || isnan(humidityPct) || humidityPct <= 0.0f) {
    return NAN;
  }

  const float humidity = clampf(humidityPct, 1.0f, 100.0f);
  constexpr float a = 17.62f;
  constexpr float b = 243.12f;
  const float gamma = logf(humidity / 100.0f) + (a * temperatureC) / (b + temperatureC);
  return (b * gamma) / (a - gamma);
}

void setPwmDuty(const float duty, const bool bypassMaxDuty = false) {
  const float maxDuty = bypassMaxDuty ? 1.0f : controlConfig.maxDuty;
  controlState.pwmDuty = clampf(duty, 0.0f, maxDuty);
  const uint32_t rawDuty = static_cast<uint32_t>(roundf(controlState.pwmDuty * pwm::MAX_DUTY));
  ledcWrite(pwm::CHANNEL, rawDuty);
}

void resetPid() {
  pidIntegral = 0.0f;
  pidLastError = 0.0f;
  pidHasHistory = false;
  controlState.pidOutput = 0.0f;
}

void applyPidProfile(const String& profile) {
  if (profile == "soft") {
    pidConfig.kp = 12.0f;
    pidConfig.ki = 0.12f;
    pidConfig.kd = 8.0f;
  } else if (profile == "aggressive") {
    pidConfig.kp = 28.0f;
    pidConfig.ki = 0.45f;
    pidConfig.kd = 15.0f;
  } else {
    pidConfig.kp = 20.0f;
    pidConfig.ki = 0.25f;
    pidConfig.kd = 12.0f;
  }

  controlState.pidProfile = profile;
  controlState.pidBaseTuned = true;
  resetPid();
}

void applyBasePidTuning() {
  const float thermalSpan = isfinite(sensorState.ambientC)
                                ? clampf(fabsf(sensorState.ambientC - controlConfig.targetC), 4.0f, 30.0f)
                                : 10.0f;
  pidConfig.kp = clampf(8.0f + thermalSpan * 1.3f, 8.0f, 40.0f);
  pidConfig.ki = clampf(0.08f + thermalSpan * 0.015f, 0.05f, 0.8f);
  pidConfig.kd = clampf(5.0f + thermalSpan * 0.55f, 4.0f, 24.0f);
  controlState.pidProfile = "base-auto";
  controlState.pidBaseTuned = true;
  resetPid();
}

void savePresetToPreferences(const char* presetName, const PresetConfig& preset) {
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putFloat((String(presetName) + "_tgt").c_str(), preset.targetC);
  preferences.putFloat((String(presetName) + "_dew").c_str(), preset.dewMarginC);
  preferences.putFloat((String(presetName) + "_dty").c_str(), preset.maxDuty);
  preferences.putFloat((String(presetName) + "_hot").c_str(), preset.maxHotSideC);
  preferences.putBool((String(presetName) + "_hen").c_str(), preset.hotSensorEnabled);
  preferences.end();
}

void loadPresetFromPreferences(const char* presetName, PresetConfig& preset) {
  preferences.begin(PREF_NAMESPACE, true);
  preset.targetC = preferences.getFloat((String(presetName) + "_tgt").c_str(), preset.targetC);
  preset.dewMarginC = preferences.getFloat((String(presetName) + "_dew").c_str(), preset.dewMarginC);
  preset.maxDuty = preferences.getFloat((String(presetName) + "_dty").c_str(), preset.maxDuty);
  preset.maxHotSideC = preferences.getFloat((String(presetName) + "_hot").c_str(), preset.maxHotSideC);
  preset.hotSensorEnabled = preferences.getBool((String(presetName) + "_hen").c_str(), preset.hotSensorEnabled);
  preferences.end();
}

PresetConfig captureCurrentPreset() {
  PresetConfig preset;
  preset.targetC = controlConfig.targetC;
  preset.dewMarginC = controlConfig.dewMarginC;
  preset.maxDuty = controlConfig.maxDuty;
  preset.maxHotSideC = controlConfig.maxHotSideC;
  preset.hotSensorEnabled = controlConfig.hotSensorEnabled;
  return preset;
}

PresetConfig* getPresetByName(const String& presetName) {
  if (presetName == PRESET_ESTATE) {
    return &presetEstate;
  }
  if (presetName == PRESET_INVERNO) {
    return &presetInverno;
  }
  if (presetName == PRESET_DEEP) {
    return &presetDeepCooling;
  }
  return nullptr;
}

bool applyPresetByName(const String& presetName) {
  PresetConfig* preset = getPresetByName(presetName);
  if (preset == nullptr) {
    return false;
  }

  controlConfig.targetC = preset->targetC;
  controlConfig.dewMarginC = preset->dewMarginC;
  controlConfig.maxDuty = clampf(preset->maxDuty, 0.0f, 1.0f);
  controlConfig.maxHotSideC = clampf(preset->maxHotSideC, 20.0f, 100.0f);
  controlConfig.hotSensorEnabled = preset->hotSensorEnabled;
  savePreferences();
  return true;
}

bool storePresetByName(const String& presetName) {
  PresetConfig* preset = getPresetByName(presetName);
  if (preset == nullptr) {
    return false;
  }

  *preset = captureCurrentPreset();
  savePresetToPreferences(presetName.c_str(), *preset);
  return true;
}

void loadPreferences() {
  preferences.begin(PREF_NAMESPACE, true);
  calibration.coldOffsetC = preferences.getFloat("cold_off", calibration.coldOffsetC);
  calibration.hotOffsetC = preferences.getFloat("hot_off", calibration.hotOffsetC);
  calibration.ambientTempOffsetC = preferences.getFloat("amb_off", calibration.ambientTempOffsetC);
  calibration.humidityOffsetPct = preferences.getFloat("hum_off", calibration.humidityOffsetPct);
  calibration.currentOffsetA = preferences.getFloat("cur_off", calibration.currentOffsetA);
  calibration.currentScale = preferences.getFloat("cur_gain", calibration.currentScale);
  pidConfig.kp = preferences.getFloat("pid_kp", pidConfig.kp);
  pidConfig.ki = preferences.getFloat("pid_ki", pidConfig.ki);
  pidConfig.kd = preferences.getFloat("pid_kd", pidConfig.kd);
  controlConfig.targetC = preferences.getFloat("target_c", controlConfig.targetC);
  controlConfig.dewMarginC = preferences.getFloat("dew_marg", controlConfig.dewMarginC);
  controlConfig.maxDuty = preferences.getFloat("max_duty", controlConfig.maxDuty);
  controlConfig.maxHotSideC = preferences.getFloat("max_hot_c", controlConfig.maxHotSideC);
  controlConfig.enabled = preferences.getBool("enabled", controlConfig.enabled);
  controlConfig.hotSensorEnabled = preferences.getBool("hot_en", controlConfig.hotSensorEnabled);
  controlState.pidBaseTuned = preferences.getBool("pid_tuned", false);
  controlState.pidProfile = preferences.getString("pid_prof", "manual");
  networkConfig.wifiSsid = preferences.getString("wifi_ssid", defaultWifiSsid());
  networkConfig.wifiPassword = preferences.getString("wifi_pass", defaultWifiPassword());
  networkConfig.hostname = preferences.getString("hostname", defaultHostname());
  networkConfig.dhcpEnabled = preferences.getBool("dhcp_en", true);
  parseIpAddressString(preferences.getString("ip_addr", ipAddressToString(networkConfig.staticIp)), networkConfig.staticIp);
  parseIpAddressString(preferences.getString("gw_addr", ipAddressToString(networkConfig.gateway)), networkConfig.gateway);
  parseIpAddressString(preferences.getString("sn_mask", ipAddressToString(networkConfig.subnet)), networkConfig.subnet);
  parseIpAddressString(preferences.getString("dns1", ipAddressToString(networkConfig.dns1)), networkConfig.dns1);
  preferences.end();

  presetEstate.targetC = 10.0f;
  presetEstate.dewMarginC = 3.0f;
  presetEstate.maxDuty = 0.65f;
  presetEstate.maxHotSideC = 48.0f;
  presetEstate.hotSensorEnabled = true;

  presetInverno.targetC = 3.0f;
  presetInverno.dewMarginC = 2.0f;
  presetInverno.maxDuty = 0.75f;
  presetInverno.maxHotSideC = 50.0f;
  presetInverno.hotSensorEnabled = true;

  presetDeepCooling.targetC = -5.0f;
  presetDeepCooling.dewMarginC = 3.0f;
  presetDeepCooling.maxDuty = 1.0f;
  presetDeepCooling.maxHotSideC = 52.0f;
  presetDeepCooling.hotSensorEnabled = true;

  loadPresetFromPreferences(PRESET_ESTATE, presetEstate);
  loadPresetFromPreferences(PRESET_INVERNO, presetInverno);
  loadPresetFromPreferences(PRESET_DEEP, presetDeepCooling);

  controlConfig.maxDuty = clampf(controlConfig.maxDuty, 0.0f, 1.0f);
  controlConfig.maxHotSideC = clampf(controlConfig.maxHotSideC, 20.0f, 100.0f);
  if (networkConfig.hostname.isEmpty()) {
    networkConfig.hostname = defaultHostname();
  }
}

void savePreferences() {
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putFloat("cold_off", calibration.coldOffsetC);
  preferences.putFloat("hot_off", calibration.hotOffsetC);
  preferences.putFloat("amb_off", calibration.ambientTempOffsetC);
  preferences.putFloat("hum_off", calibration.humidityOffsetPct);
  preferences.putFloat("cur_off", calibration.currentOffsetA);
  preferences.putFloat("cur_gain", calibration.currentScale);
  preferences.putFloat("pid_kp", pidConfig.kp);
  preferences.putFloat("pid_ki", pidConfig.ki);
  preferences.putFloat("pid_kd", pidConfig.kd);
  preferences.putFloat("target_c", controlConfig.targetC);
  preferences.putFloat("dew_marg", controlConfig.dewMarginC);
  preferences.putFloat("max_duty", controlConfig.maxDuty);
  preferences.putFloat("max_hot_c", controlConfig.maxHotSideC);
  preferences.putBool("enabled", controlConfig.enabled);
  preferences.putBool("hot_en", controlConfig.hotSensorEnabled);
  preferences.putBool("pid_tuned", controlState.pidBaseTuned);
  preferences.putString("pid_prof", controlState.pidProfile);
  preferences.putString("wifi_ssid", networkConfig.wifiSsid);
  preferences.putString("wifi_pass", networkConfig.wifiPassword);
  preferences.putString("hostname", networkConfig.hostname);
  preferences.putBool("dhcp_en", networkConfig.dhcpEnabled);
  preferences.putString("ip_addr", ipAddressToString(networkConfig.staticIp));
  preferences.putString("gw_addr", ipAddressToString(networkConfig.gateway));
  preferences.putString("sn_mask", ipAddressToString(networkConfig.subnet));
  preferences.putString("dns1", ipAddressToString(networkConfig.dns1));
  preferences.end();
}

void refreshSensors() {
#if !USE_ADS1115
  sensorState.adc32Raw = readEspAnalogRaw(pins::CURRENT_ADC);
  sensorState.adc33Raw = readEspAnalogRaw(pins::COLD_NTC_ADC);
  sensorState.adc34Raw = readEspAnalogRaw(pins::HOT_NTC_ADC);
#if CONFIG_IDF_TARGET_ESP32
  sensorState.adc32IdfRaw = readAdc1Raw(ADC1_CHANNEL_4);
  sensorState.adc33IdfRaw = readAdc1Raw(ADC1_CHANNEL_5);
  sensorState.adc34IdfRaw = readAdc1Raw(ADC1_CHANNEL_0);
#endif
  sensorState.adc32VoltageV = espRawToVoltageV(sensorState.adc32Raw);
  sensorState.adc33VoltageV = espRawToVoltageV(sensorState.adc33Raw);
  sensorState.adc34VoltageV = espRawToVoltageV(sensorState.adc34Raw);
  sensorState.coldNtcRaw = readEspAnalogRaw(pins::COLD_NTC_ADC);
  sensorState.hotNtcRaw = readEspAnalogRaw(pins::HOT_NTC_ADC);
  sensorState.coldNtcVoltageV = espRawToVoltageV(sensorState.coldNtcRaw);
  sensorState.hotNtcVoltageV = espRawToVoltageV(sensorState.hotNtcRaw);
#else
  sensorState.coldNtcVoltageV = readAnalogVoltageV(
      ads_channels::COLD_NTC
  );
  sensorState.hotNtcVoltageV = readAnalogVoltageV(
      ads_channels::HOT_NTC
  );
#endif
  sensorState.coldNtcResistanceOhm = thermistorResistanceFromVoltageOhm(sensorState.coldNtcVoltageV);
  const float coldNtcC = thermistorTemperatureFromVoltageC(sensorState.coldNtcVoltageV);
  sensorState.coldNtcOk = !isnan(coldNtcC);
  sensorState.coldRawC = sensorState.coldNtcOk ? coldNtcC + calibration.coldOffsetC : NAN;
  sensorState.coldC = filterTemperatureC(sensorState.coldRawC, sensorState.coldC);

  sensorState.hotNtcResistanceOhm = thermistorResistanceFromVoltageOhm(sensorState.hotNtcVoltageV);
  const float hotNtcC = thermistorTemperatureFromVoltageC(sensorState.hotNtcVoltageV);
  sensorState.hotNtcOk = !isnan(hotNtcC);
  sensorState.hotRawC = sensorState.hotNtcOk ? hotNtcC + calibration.hotOffsetC : NAN;
  sensorState.hotC = filterTemperatureC(sensorState.hotRawC, sensorState.hotC);
  updateDebugTemperatureMinMax();
  updateDebugTemperatureTrend();

  if (ambientSensorPresent) {
#if USE_SHT30
    const float ambientC = ambientSensor.readTemperature();
    const float humidity = ambientSensor.readHumidity();
#else
    const float ambientC = bme.readTemperature();
    const float humidity = bme.readHumidity();
#endif
    sensorState.ambientSensorOk = isfinite(ambientC) && isfinite(humidity) && humidity >= 0.0f && humidity <= 100.0f;
    sensorState.bmeOk = sensorState.ambientSensorOk;
    sensorState.ambientC = sensorState.ambientSensorOk ? ambientC + calibration.ambientTempOffsetC : NAN;
    sensorState.humidityPct = sensorState.ambientSensorOk ? clampf(humidity + calibration.humidityOffsetPct, 0.0f, 100.0f) : NAN;
    sensorState.dewPointC = sensorState.ambientSensorOk ? computeDewPointC(sensorState.ambientC, sensorState.humidityPct) : NAN;
  } else {
    sensorState.ambientSensorOk = false;
    sensorState.bmeOk = false;
    sensorState.ambientC = NAN;
    sensorState.humidityPct = NAN;
    sensorState.dewPointC = NAN;
  }

#if USE_ACS71X_CURRENT
  const float currentVoltage = readAnalogVoltageV(
#if USE_ADS1115
      ads_channels::CURRENT
#else
      pins::CURRENT_ADC
#endif
  );
  sensorState.currentOk = isfinite(currentVoltage);
  sensorState.inaOk = false;
  if (sensorState.currentOk) {
    const float rawCurrentA = ((currentVoltage - currentSensorConfig.zeroVoltageV) * 1000.0f) /
                              currentSensorConfig.sensitivityMvPerA;
    sensorState.currentA = (rawCurrentA * calibration.currentScale) + calibration.currentOffsetA;
  } else {
    sensorState.currentA = NAN;
  }
  sensorState.busVoltageV = NAN;
#else
  if (ina219Present) {
    const float shuntVoltageMv = ina219.getShuntVoltage_mV();
    const float busVoltage = ina219.getBusVoltage_V();
    const float currentA = (shuntVoltageMv / 1000.0f) / INA219_SHUNT_RESISTOR_OHM;
    sensorState.inaOk = isfinite(currentA) && isfinite(busVoltage) && isfinite(shuntVoltageMv);
    sensorState.currentOk = sensorState.inaOk;
    sensorState.shuntVoltageMv = sensorState.inaOk ? shuntVoltageMv : NAN;
    sensorState.currentA = sensorState.inaOk ? (currentA * calibration.currentScale) + calibration.currentOffsetA : NAN;
    sensorState.busVoltageV = sensorState.inaOk ? busVoltage : NAN;
  } else {
    sensorState.inaOk = false;
    sensorState.currentOk = false;
    sensorState.currentA = NAN;
    sensorState.busVoltageV = NAN;
    sensorState.shuntVoltageMv = NAN;
  }
#endif
}

void updateControl(const float dtSeconds) {
  controlState.hotProtectionActive = false;

  if (controlConfig.hotSensorEnabled && sensorState.hotNtcOk && sensorState.hotC >= controlConfig.maxHotSideC) {
    controlState.hotProtectionActive = true;
    controlState.effectiveTargetC = controlConfig.targetC;
    controlState.dewClampActive = false;
    setPwmDuty(0.0f);
    resetPid();
    return;
  }

  if (debugState.manualPwmEnabled) {
    controlState.effectiveTargetC = controlConfig.targetC;
    controlState.dewClampActive = false;
    controlState.pidOutput = debugState.manualPwmDuty * 100.0f;
    setPwmDuty(debugState.manualPwmDuty, true);
    resetPid();
    return;
  }

  if (!controlConfig.enabled || !sensorState.coldNtcOk) {
    controlState.effectiveTargetC = controlConfig.targetC;
    controlState.dewClampActive = false;
    setPwmDuty(0.0f);
    resetPid();
    return;
  }

  float effectiveTargetC = controlConfig.targetC;
  bool dewClampActive = false;
  if (isfinite(sensorState.dewPointC)) {
    const float minimumSafeTarget = sensorState.dewPointC + controlConfig.dewMarginC;
    if (effectiveTargetC < minimumSafeTarget) {
      effectiveTargetC = minimumSafeTarget;
      dewClampActive = true;
    }
  }

  controlState.effectiveTargetC = effectiveTargetC;
  controlState.dewClampActive = dewClampActive;

  const float error = sensorState.coldC - effectiveTargetC;
  pidIntegral = clampf(pidIntegral + (error * dtSeconds), -100.0f, 100.0f);
  const float derivative = pidHasHistory ? (error - pidLastError) / dtSeconds : 0.0f;
  pidHasHistory = true;
  pidLastError = error;

  float output = (pidConfig.kp * error) + (pidConfig.ki * pidIntegral) + (pidConfig.kd * derivative);
  output = clampf(output, 0.0f, 100.0f);
  controlState.pidOutput = output;
  setPwmDuty(output / 100.0f);
}

void appendStatusJson(JsonDocument& doc) {
  JsonObject sensor = doc["sensor"].to<JsonObject>();
  sensor["cold_c"] = sensorState.coldC;
  sensor["hot_c"] = sensorState.hotC;
  sensor["cold_raw_c"] = sensorState.coldRawC;
  sensor["hot_raw_c"] = sensorState.hotRawC;
  sensor["ambient_c"] = sensorState.ambientC;
  sensor["humidity_pct"] = sensorState.humidityPct;
  sensor["dew_point_c"] = sensorState.dewPointC;
  sensor["current_a"] = sensorState.currentA;
  sensor["bus_voltage_v"] = sensorState.busVoltageV;
  sensor["shunt_voltage_mv"] = sensorState.shuntVoltageMv;
  sensor["cold_ntc_voltage_v"] = sensorState.coldNtcVoltageV;
  sensor["hot_ntc_voltage_v"] = sensorState.hotNtcVoltageV;
  sensor["cold_ntc_ohm"] = sensorState.coldNtcResistanceOhm;
  sensor["hot_ntc_ohm"] = sensorState.hotNtcResistanceOhm;
  sensor["cold_ntc_raw"] = sensorState.coldNtcRaw;
  sensor["hot_ntc_raw"] = sensorState.hotNtcRaw;
  sensor["adc32_raw"] = sensorState.adc32Raw;
  sensor["adc33_raw"] = sensorState.adc33Raw;
  sensor["adc34_raw"] = sensorState.adc34Raw;
  sensor["adc32_idf_raw"] = sensorState.adc32IdfRaw;
  sensor["adc33_idf_raw"] = sensorState.adc33IdfRaw;
  sensor["adc34_idf_raw"] = sensorState.adc34IdfRaw;
  sensor["adc32_voltage_v"] = sensorState.adc32VoltageV;
  sensor["adc33_voltage_v"] = sensorState.adc33VoltageV;
  sensor["adc34_voltage_v"] = sensorState.adc34VoltageV;
  sensor["cold_ntc_ok"] = sensorState.coldNtcOk;
  sensor["hot_ntc_ok"] = sensorState.hotNtcOk;
  sensor["ambient_sensor_type"] = USE_SHT30 ? "sht30" : "bme280";
  sensor["ambient_sensor_ok"] = sensorState.ambientSensorOk;
  sensor["bme280_ok"] = sensorState.bmeOk;
  sensor["ina219_ok"] = sensorState.inaOk;
  sensor["ads1115_ok"] = sensorState.adsOk;
  sensor["current_sensor_ok"] = sensorState.currentOk;

  JsonObject control = doc["control"].to<JsonObject>();
  control["enabled"] = controlConfig.enabled;
  control["target_c"] = controlConfig.targetC;
  control["effective_target_c"] = controlState.effectiveTargetC;
  control["dew_margin_c"] = controlConfig.dewMarginC;
  control["dew_clamp_active"] = controlState.dewClampActive;
  control["hot_sensor_enabled"] = controlConfig.hotSensorEnabled;
  control["hot_protection_active"] = controlState.hotProtectionActive;
  control["max_hot_side_c"] = controlConfig.maxHotSideC;
  control["pwm_duty"] = controlState.pwmDuty;
  control["pid_output_pct"] = controlState.pidOutput;
  control["max_duty"] = controlConfig.maxDuty;

  JsonObject debug = doc["debug"].to<JsonObject>();
  debug["manual_pwm_enabled"] = debugState.manualPwmEnabled;
  debug["manual_pwm_duty"] = debugState.manualPwmDuty;
  debug["manual_pwm_pct"] = debugState.manualPwmDuty * 100.0f;
  debug["cold_min_c"] = debugState.coldMinC;
  debug["cold_max_c"] = debugState.coldMaxC;
  debug["hot_min_c"] = debugState.hotMinC;
  debug["hot_max_c"] = debugState.hotMaxC;
  debug["cold_trend_c_per_min"] = debugState.coldTrendCPerMin;
  debug["hot_trend_c_per_min"] = debugState.hotTrendCPerMin;
  debug["peltier_trend_power_pct"] = debugState.peltierTrendPowerPct;
  debug["temperature_filter_alpha"] = NTC_TEMP_FILTER_ALPHA;
  debug["adc_sample_count"] = NTC_ADC_SAMPLE_COUNT;
  debug["i2c_device_count"] = i2cDeviceCount;
  debug["i2c_scan"] = i2cScanSummary;

  JsonObject pid = doc["pid"].to<JsonObject>();
  pid["kp"] = pidConfig.kp;
  pid["ki"] = pidConfig.ki;
  pid["kd"] = pidConfig.kd;
  pid["profile"] = controlState.pidProfile;
  pid["base_tuned"] = controlState.pidBaseTuned;

  JsonObject calibrationJson = doc["calibration"].to<JsonObject>();
  calibrationJson["cold_offset_c"] = calibration.coldOffsetC;
  calibrationJson["hot_offset_c"] = calibration.hotOffsetC;
  calibrationJson["ambient_offset_c"] = calibration.ambientTempOffsetC;
  calibrationJson["humidity_offset_pct"] = calibration.humidityOffsetPct;
  calibrationJson["current_offset_a"] = calibration.currentOffsetA;
  calibrationJson["current_scale"] = calibration.currentScale;

  JsonObject network = doc["network"].to<JsonObject>();
  network["wifi_connected"] = WiFi.isConnected();
  network["ip"] = WiFi.isConnected() ? WiFi.localIP().toString() : String();
  network["hostname"] = networkConfig.hostname;
  network["mdns_url"] = mdnsStarted ? String("http://") + networkConfig.hostname + ".local/" : String();
  network["ble_name"] = networkConfig.hostname;
  network["wifi_ssid"] = networkConfig.wifiSsid;
  network["dhcp_enabled"] = networkConfig.dhcpEnabled;
  network["static_ip"] = ipAddressToString(networkConfig.staticIp);
  network["gateway"] = ipAddressToString(networkConfig.gateway);
  network["subnet"] = ipAddressToString(networkConfig.subnet);
  network["dns1"] = ipAddressToString(networkConfig.dns1);
  network["captive_portal_active"] = captivePortalActive;
  network["ap_ssid"] = String(networkConfig.hostname) + "-setup";
  network["ap_ip"] = CAPTIVE_IP.toString();

  JsonObject board = doc["board"].to<JsonObject>();
  board["firmware_version"] = FW_VERSION;
  board["project_url"] = PROJECT_URL;
  board["adc_backend"] = USE_ADS1115 ? "ads1115" : "esp32_adc";
  board["current_sensor"] = USE_ACS71X_CURRENT ? "acs71x" : "ina219";
  board["free_heap_bytes"] = ESP.getFreeHeap();
  board["wifi_rssi_dbm"] = WiFi.isConnected() ? WiFi.RSSI() : 0;
  board["wifi_bssid"] = WiFi.isConnected() ? WiFi.BSSIDstr() : String();
  #if ENABLE_BLE
  board["ble_connected"] = bleServer != nullptr ? bleServer->getConnectedCount() > 0 : false;
  board["ble_clients"] = bleServer != nullptr ? bleServer->getConnectedCount() : 0;
  #else
  board["ble_connected"] = false;
  board["ble_clients"] = 0;
  #endif
  board["cpu_temp_c"] = temperatureRead();
  board["boot_time_local"] = formatLocalTimeString(bootEpoch);
  board["uptime_s"] = millis() / 1000UL;

  JsonObject presets = doc["presets"].to<JsonObject>();
  JsonObject estate = presets[PRESET_ESTATE].to<JsonObject>();
  estate["target_c"] = presetEstate.targetC;
  estate["dew_margin_c"] = presetEstate.dewMarginC;
  estate["max_duty"] = presetEstate.maxDuty;
  estate["max_hot_side_c"] = presetEstate.maxHotSideC;
  estate["hot_sensor_enabled"] = presetEstate.hotSensorEnabled;

  JsonObject inverno = presets[PRESET_INVERNO].to<JsonObject>();
  inverno["target_c"] = presetInverno.targetC;
  inverno["dew_margin_c"] = presetInverno.dewMarginC;
  inverno["max_duty"] = presetInverno.maxDuty;
  inverno["max_hot_side_c"] = presetInverno.maxHotSideC;
  inverno["hot_sensor_enabled"] = presetInverno.hotSensorEnabled;

  JsonObject deep = presets[PRESET_DEEP].to<JsonObject>();
  deep["target_c"] = presetDeepCooling.targetC;
  deep["dew_margin_c"] = presetDeepCooling.dewMarginC;
  deep["max_duty"] = presetDeepCooling.maxDuty;
  deep["max_hot_side_c"] = presetDeepCooling.maxHotSideC;
  deep["hot_sensor_enabled"] = presetDeepCooling.hotSensorEnabled;
}

String buildStatusJson() {
  JsonDocument doc;
  appendStatusJson(doc);
  String payload;
  serializeJson(doc, payload);
  return payload;
}

#if ENABLE_BLE
String buildBleTelemetryJson() {
  JsonDocument doc;
  doc["cold_c"] = sensorState.coldC;
  doc["hot_c"] = sensorState.hotC;
  doc["ambient_c"] = sensorState.ambientC;
  doc["humidity_pct"] = sensorState.humidityPct;
  doc["dew_point_c"] = sensorState.dewPointC;
  doc["current_a"] = sensorState.currentA;
  doc["power_pct"] = controlState.pwmDuty * 100.0f;
  doc["enabled"] = controlConfig.enabled;
  doc["target_c"] = controlConfig.targetC;
  doc["effective_target_c"] = controlState.effectiveTargetC;
  doc["hot_protection"] = controlState.hotProtectionActive;
  doc["dew_clamp"] = controlState.dewClampActive;
  String payload;
  serializeJson(doc, payload);
  return payload;
}

void setBleBoolValue(NimBLECharacteristic* characteristic, const bool value) {
  if (characteristic == nullptr) {
    return;
  }
  const uint8_t encoded = value ? 1U : 0U;
  characteristic->setValue(&encoded, 1);
}

void setBleInt16Value(NimBLECharacteristic* characteristic, const int16_t value) {
  if (characteristic == nullptr) {
    return;
  }
  characteristic->setValue(reinterpret_cast<const uint8_t*>(&value), sizeof(value));
}

bool decodeBleBool(const std::string& value, bool& decoded) {
  if (value.empty()) {
    return false;
  }
  decoded = value[0] != 0;
  return true;
}

bool decodeBleInt16(const std::string& value, int16_t& decoded) {
  if (value.size() < sizeof(decoded)) {
    return false;
  }
  memcpy(&decoded, value.data(), sizeof(decoded));
  return true;
}

void syncBleCharacteristics(const bool notify) {
  if (bleStatusCharacteristic != nullptr) {
    const String payload = buildStatusJson();
    bleStatusCharacteristic->setValue(payload.c_str());
    if (notify) {
      bleStatusCharacteristic->notify();
    }
  }

  if (bleTelemetryCharacteristic != nullptr) {
    const String telemetry = buildBleTelemetryJson();
    bleTelemetryCharacteristic->setValue(telemetry.c_str());
    if (notify) {
      bleTelemetryCharacteristic->notify();
    }
  }

  setBleBoolValue(bleEnabledCharacteristic, controlConfig.enabled);
  setBleInt16Value(bleTargetCharacteristic, static_cast<int16_t>(roundf(controlConfig.targetC * 100.0f)));

  if (notify && bleEnabledCharacteristic != nullptr) {
    bleEnabledCharacteristic->notify();
  }
  if (notify && bleTargetCharacteristic != nullptr) {
    bleTargetCharacteristic->notify();
  }
}
#else
void syncBleCharacteristics(const bool notify) {
  (void)notify;
}
#endif

void logStatusToSerial() {
  Serial.printf(
      "[status] en=%d cold=%.2fC hot=%.2fC amb=%.2fC hum=%.1f%% dew=%.2fC tgt=%.2fC eff=%.2fC duty=%.1f%% I=%.2fA hotProt=%d dewClamp=%d pid(%.2f/%.2f/%.2f)\n",
      controlConfig.enabled,
      sensorState.coldC,
      sensorState.hotC,
      sensorState.ambientC,
      sensorState.humidityPct,
      sensorState.dewPointC,
      controlConfig.targetC,
      controlState.effectiveTargetC,
      controlState.pwmDuty * 100.0f,
      sensorState.currentA,
      controlState.hotProtectionActive,
      controlState.dewClampActive,
      pidConfig.kp,
      pidConfig.ki,
      pidConfig.kd);
}

bool applyJsonConfig(const JsonDocument& doc, String& message) {
  bool changed = false;
  bool debugChanged = false;
  const auto tryParseNumber = [&](const JsonVariantConst& value, float& outValue) {
    if (value.isNull()) {
      return false;
    }
    if (value.is<int>() || value.is<float>() || value.is<double>()) {
      outValue = value.as<float>();
      return true;
    }

    String normalized = value.as<String>();
    normalized.trim();
    if (normalized.startsWith("\"") && normalized.endsWith("\"") && normalized.length() >= 2) {
      normalized = normalized.substring(1, normalized.length() - 1);
      normalized.trim();
    }
    normalized.replace(',', '.');
    if (normalized.isEmpty()) {
      return false;
    }

    char* endPtr = nullptr;
    const float parsed = strtof(normalized.c_str(), &endPtr);
    if (endPtr == normalized.c_str() || (endPtr != nullptr && *endPtr != '\0')) {
      return false;
    }
    outValue = parsed;
    return true;
  };

  float parsedValue = 0.0f;

  if (doc["enabled"].is<bool>()) {
    controlConfig.enabled = doc["enabled"].as<bool>();
    changed = true;
  }
  if (doc["hot_sensor_enabled"].is<bool>()) {
    controlConfig.hotSensorEnabled = doc["hot_sensor_enabled"].as<bool>();
    changed = true;
  }
  if (tryParseNumber(doc["target_c"], parsedValue)) {
    controlConfig.targetC = parsedValue;
    changed = true;
  }
  if (tryParseNumber(doc["dew_margin_c"], parsedValue)) {
    controlConfig.dewMarginC = clampf(parsedValue, 0.0f, 20.0f);
    changed = true;
  }
  if (tryParseNumber(doc["max_duty"], parsedValue)) {
    controlConfig.maxDuty = clampf(parsedValue, 0.0f, 1.0f);
    changed = true;
  }
  if (tryParseNumber(doc["max_hot_side_c"], parsedValue)) {
    controlConfig.maxHotSideC = clampf(parsedValue, 20.0f, 100.0f);
    changed = true;
  }

  if (doc["pid_profile"].is<const char*>()) {
    applyPidProfile(String(doc["pid_profile"].as<const char*>()));
    changed = true;
  }
  if (doc["pid_base_tune"].is<bool>() && doc["pid_base_tune"].as<bool>()) {
    applyBasePidTuning();
    changed = true;
  }

  if (doc["pid"].is<JsonObject>()) {
    const JsonObjectConst pid = doc["pid"].as<JsonObjectConst>();
    if (tryParseNumber(pid["kp"], parsedValue)) {
      pidConfig.kp = parsedValue;
      changed = true;
    }
    if (tryParseNumber(pid["ki"], parsedValue)) {
      pidConfig.ki = parsedValue;
      changed = true;
    }
    if (tryParseNumber(pid["kd"], parsedValue)) {
      pidConfig.kd = parsedValue;
      changed = true;
    }
    if (changed) {
      controlState.pidProfile = "manual";
    }
  }

  if (doc["debug"].is<JsonObject>()) {
    const JsonObjectConst debug = doc["debug"].as<JsonObjectConst>();
    if (debug["manual_pwm_enabled"].is<bool>()) {
      debugState.manualPwmEnabled = debug["manual_pwm_enabled"].as<bool>();
      changed = true;
      debugChanged = true;
    }
    if (tryParseNumber(debug["manual_pwm_duty"], parsedValue)) {
      debugState.manualPwmDuty = clampf(parsedValue, 0.0f, 1.0f);
      changed = true;
      debugChanged = true;
    }
    if (tryParseNumber(debug["manual_pwm_pct"], parsedValue)) {
      debugState.manualPwmDuty = clampf(parsedValue / 100.0f, 0.0f, 1.0f);
      changed = true;
      debugChanged = true;
    }
    if (debug["reset_temp_minmax"].is<bool>() && debug["reset_temp_minmax"].as<bool>()) {
      resetDebugTemperatureMinMax();
      updateDebugTemperatureMinMax();
      changed = true;
    }
  }

  if (doc["manual_pwm_enabled"].is<bool>()) {
    debugState.manualPwmEnabled = doc["manual_pwm_enabled"].as<bool>();
    changed = true;
    debugChanged = true;
  }
  if (tryParseNumber(doc["manual_pwm_duty"], parsedValue)) {
    debugState.manualPwmDuty = clampf(parsedValue, 0.0f, 1.0f);
    changed = true;
    debugChanged = true;
  }
  if (tryParseNumber(doc["manual_pwm_pct"], parsedValue)) {
    debugState.manualPwmDuty = clampf(parsedValue / 100.0f, 0.0f, 1.0f);
    changed = true;
    debugChanged = true;
  }
  if (doc["reset_temp_minmax"].is<bool>() && doc["reset_temp_minmax"].as<bool>()) {
    resetDebugTemperatureMinMax();
    updateDebugTemperatureMinMax();
    changed = true;
  }

  const JsonVariantConst calibrationNode = doc["calibration"];
  if (!calibrationNode.isNull()) {
    const JsonVariantConst cal = calibrationNode;
    if (tryParseNumber(cal["cold_offset_c"], parsedValue)) {
      calibration.coldOffsetC = clampf(parsedValue, -5.0f, 5.0f);
      changed = true;
    }
    if (tryParseNumber(cal["hot_offset_c"], parsedValue)) {
      calibration.hotOffsetC = clampf(parsedValue, -5.0f, 5.0f);
      changed = true;
    }
    if (tryParseNumber(cal["ambient_offset_c"], parsedValue)) {
      calibration.ambientTempOffsetC = clampf(parsedValue, -5.0f, 5.0f);
      changed = true;
    }
    if (tryParseNumber(cal["humidity_offset_pct"], parsedValue)) {
      calibration.humidityOffsetPct = clampf(parsedValue, -10.0f, 10.0f);
      changed = true;
    }
    if (tryParseNumber(cal["current_offset_a"], parsedValue)) {
      calibration.currentOffsetA = parsedValue;
      changed = true;
    }
    if (tryParseNumber(cal["current_scale"], parsedValue)) {
      calibration.currentScale = parsedValue;
      changed = true;
    }
  }

  if (!changed) {
    message = "Nessun parametro valido ricevuto";
    return false;
  }

  if (debugChanged) {
    resetPid();
    controlState.dewClampActive = false;
    controlState.pidOutput = debugState.manualPwmEnabled ? debugState.manualPwmDuty * 100.0f : 0.0f;
    setPwmDuty(debugState.manualPwmEnabled ? debugState.manualPwmDuty : 0.0f, debugState.manualPwmEnabled);
  }

  savePreferences();
  syncBleCharacteristics(true);
  message = "Configurazione aggiornata";
  return true;
}

bool applyNetworkJson(const JsonDocument& doc, String& message) {
  const JsonVariantConst source = doc["network"].is<JsonObject>() ? doc["network"] : doc.as<JsonVariantConst>();
  bool changed = false;

  if (source["wifi_ssid"].is<const char*>()) {
    networkConfig.wifiSsid = source["wifi_ssid"].as<const char*>();
    changed = true;
  }
  if (source["wifi_password"].is<const char*>()) {
    networkConfig.wifiPassword = source["wifi_password"].as<const char*>();
    changed = true;
  }
  if (source["hostname"].is<const char*>()) {
    networkConfig.hostname = source["hostname"].as<const char*>();
    networkConfig.hostname.trim();
    if (networkConfig.hostname.isEmpty()) {
      networkConfig.hostname = defaultHostname();
    }
    changed = true;
  }
  if (source["dhcp_enabled"].is<bool>()) {
    networkConfig.dhcpEnabled = source["dhcp_enabled"].as<bool>();
    changed = true;
  }

  auto parseField = [&](const char* key, IPAddress& target) {
    if (source[key].is<const char*>()) {
      IPAddress parsed;
      if (!parseIpAddressString(source[key].as<const char*>(), parsed)) {
        message = String("Indirizzo IP non valido: ") + key;
        return false;
      }
      target = parsed;
      changed = true;
    }
    return true;
  };

  if (!parseField("static_ip", networkConfig.staticIp) || !parseField("gateway", networkConfig.gateway) ||
      !parseField("subnet", networkConfig.subnet) || !parseField("dns1", networkConfig.dns1)) {
    return false;
  }

  if (!changed) {
    message = "Nessun parametro rete valido ricevuto";
    return false;
  }

  savePreferences();
  reconnectWifi();
  message = "Configurazione rete aggiornata";
  return true;
}

String buildPortalHtml() {
  String html;
  html.reserve(8500);
  html += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>ASI585MC Cooler Setup</title><style>body{margin:0;font-family:Arial,sans-serif;background:#0b1320;color:#e7eef7}.wrap{max-width:760px;margin:auto;padding:24px}.card{background:#142033;border-radius:16px;padding:20px;box-shadow:0 12px 30px rgba(0,0,0,.25)}label{display:block;margin:10px 0 6px;color:#9fb5d8}input,select{width:100%;box-sizing:border-box;font-size:16px;padding:10px;border-radius:10px;border:1px solid #456;background:#091221;color:#e7eef7}button{margin-top:14px;background:#3a86ff;color:white;border:none;border-radius:10px;padding:10px 14px;cursor:pointer}.pw{display:flex;gap:8px;align-items:center}.pw input{flex:1}.pw button{margin-top:0;white-space:nowrap;background:#223754}.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.meta{color:#9fb5d8;font-size:13px}@media(max-width:640px){.row{grid-template-columns:1fr}}</style></head><body>");
  html += F("<div class='wrap'><div class='card'><h1>Onboarding rete</h1><p class='meta'>Configura WiFi e rete del controller. Quando la connessione sara attiva, il dispositivo uscira dalla modalita setup.</p>");
  html += F("<div class='row'><div><label>SSID WiFi</label><select id='wifi_ssid'><option value=''>Scansione in corso...</option></select><button onclick='scanWifi()' type='button'>Scansiona reti</button></div><div><label>Password WiFi</label><div class='pw'><input id='wifi_password' type='password'><button onclick='togglePassword()' type='button'>Mostra</button></div></div></div>");
  html += F("<div class='row'><div><label>Hostname</label><input id='hostname'></div><div><label>Modalita IP</label><select id='dhcp_enabled'><option value='true'>DHCP</option><option value='false'>IP statico</option></select></div></div>");
  html += F("<div class='row'><div><label>IP statico</label><input id='static_ip'></div><div><label>Gateway</label><input id='gateway'></div></div><div class='row'><div><label>Subnet</label><input id='subnet'></div><div><label>DNS</label><input id='dns1'></div></div>");
  html += F("<button onclick='saveNetwork()'>Salva e collega</button> <button onclick=\"location.href='/settings'\">Settings</button><pre id='status' class='meta'>loading...</pre></div></div>");
  html += F("<script>function fill(id,v){document.getElementById(id).value=(v===undefined||v===null)?'':v}function setWifiOptions(list,current){const sel=document.getElementById('wifi_ssid');sel.innerHTML='';if(current){const cur=document.createElement('option');cur.value=current;cur.textContent=current+' (salvata)';sel.appendChild(cur)}if(!Array.isArray(list)||list.length===0){const o=document.createElement('option');o.value='';o.textContent='Nessuna rete trovata';sel.appendChild(o);if(current){sel.value=current}return}for(const net of list){const o=document.createElement('option');o.value=net.ssid;o.textContent=`${net.ssid} (${net.rssi} dBm${net.open?', open':''})`;sel.appendChild(o)}if(current){sel.value=current}}function togglePassword(){const el=document.getElementById('wifi_password');el.type=el.type==='password'?'text':'password'}async function scanWifi(){const s=document.getElementById('status');s.textContent='Scansione reti WiFi in corso...';const current=document.getElementById('wifi_ssid').value;const r=await fetch('/api/wifi/scan');const list=await r.json();setWifiOptions(list,current);if(!Array.isArray(list)||list.length===0){s.textContent='Nessuna rete trovata. Riprova tra qualche secondo.';return;}s.textContent='Reti trovate: '+list.length}async function refresh(){const r=await fetch('/api/status');const j=await r.json();setWifiOptions([],j.network.wifi_ssid);fill('hostname',j.network.hostname);fill('static_ip',j.network.static_ip);fill('gateway',j.network.gateway);fill('subnet',j.network.subnet);fill('dns1',j.network.dns1);document.getElementById('dhcp_enabled').value=j.network.dhcp_enabled?'true':'false';document.getElementById('status').textContent='Setup pronto'}async function saveNetwork(){const body={wifi_ssid:wifi_ssid.value,wifi_password:wifi_password.value,hostname:hostname.value,dhcp_enabled:dhcp_enabled.value==='true',static_ip:static_ip.value,gateway:gateway.value,subnet:subnet.value,dns1:dns1.value};const r=await fetch('/api/network',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});const j=await r.json();document.getElementById('status').textContent=j.error?j.error:'Configurazione salvata, tentativo di connessione in corso';setTimeout(async()=>{await refresh();await scanWifi();},1500)}refresh().then(scanWifi);</script></body></html>");
  return html;
}

String buildSettingsHtml() {
  String html;
  html.reserve(10500);
  html += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>ASI585MC Network Settings</title><style>body{margin:0;font-family:Arial,sans-serif;background:#08111f;color:#e8f0ff}.wrap{max-width:960px;margin:auto;padding:20px}.card{background:#111d32;border:1px solid #243757;border-radius:16px;padding:16px;box-shadow:0 10px 30px rgba(0,0,0,.25)}label{display:block;font-size:13px;color:#9fb5d8;margin:10px 0 6px}input,select{width:100%;box-sizing:border-box;background:#091221;color:#e8f0ff;border:1px solid #30496e;border-radius:10px;padding:10px}button{background:#3385ff;color:white;border:none;border-radius:10px;padding:10px 14px;cursor:pointer;margin-top:12px}.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.meta{color:#9fb5d8;font-size:13px}pre{white-space:pre-wrap;background:#091221;border-radius:12px;padding:14px;overflow:auto}@media(max-width:640px){.row{grid-template-columns:1fr}}</style></head><body>");
  html += F("<div class='wrap'><div class='card'><h1>Network Settings</h1><p class='meta'>Qui puoi salvare tutti i parametri di onboarding e connettivita.</p><div class='row'><div><label>SSID WiFi</label><select id='wifi_ssid'><option value=''>Scansione in corso...</option></select><button onclick='scanWifi()' type='button'>Scansiona reti</button></div><div><label>Password WiFi</label><div class='pw'><input id='wifi_password' type='password'><button onclick='togglePassword()' type='button'>Mostra</button></div></div></div><div class='row'><div><label>Hostname</label><input id='hostname'></div><div><label>Modalita IP</label><select id='dhcp_enabled'><option value='true'>DHCP</option><option value='false'>IP statico</option></select></div></div><div class='row'><div><label>IP statico</label><input id='static_ip'></div><div><label>Gateway</label><input id='gateway'></div></div><div class='row'><div><label>Subnet</label><input id='subnet'></div><div><label>DNS</label><input id='dns1'></div></div><button onclick='saveNetwork()'>Salva rete</button> <button onclick=\"location.href='/'\">Dashboard</button><pre id='status'>loading...</pre></div></div>");
  html += F("<script>function fill(id,v){document.getElementById(id).value=(v===undefined||v===null)?'':v}function setWifiOptions(list,current){const sel=document.getElementById('wifi_ssid');sel.innerHTML='';if(current){const cur=document.createElement('option');cur.value=current;cur.textContent=current+' (salvata)';sel.appendChild(cur)}if(!Array.isArray(list)||list.length===0){const o=document.createElement('option');o.value='';o.textContent='Nessuna rete trovata';sel.appendChild(o);if(current){sel.value=current}return}for(const net of list){const o=document.createElement('option');o.value=net.ssid;o.textContent=`${net.ssid} (${net.rssi} dBm${net.open?', open':''})`;sel.appendChild(o)}if(current){sel.value=current}}function togglePassword(){const el=document.getElementById('wifi_password');el.type=el.type==='password'?'text':'password'}async function scanWifi(){const s=document.getElementById('status');s.textContent='Scansione reti WiFi in corso...';const current=document.getElementById('wifi_ssid').value;const r=await fetch('/api/wifi/scan');const list=await r.json();setWifiOptions(list,current);if(!Array.isArray(list)||list.length===0){s.textContent='Nessuna rete trovata. Riprova tra qualche secondo.';return;}s.textContent='Reti trovate: '+list.length}async function refresh(){const r=await fetch('/api/status');const j=await r.json();setWifiOptions([],j.network.wifi_ssid);fill('hostname',j.network.hostname);fill('static_ip',j.network.static_ip);fill('gateway',j.network.gateway);fill('subnet',j.network.subnet);fill('dns1',j.network.dns1);document.getElementById('dhcp_enabled').value=j.network.dhcp_enabled?'true':'false';document.getElementById('status').textContent='Impostazioni pronte'}async function saveNetwork(){const body={wifi_ssid:wifi_ssid.value,wifi_password:wifi_password.value,hostname:hostname.value,dhcp_enabled:dhcp_enabled.value==='true',static_ip:static_ip.value,gateway:gateway.value,subnet:subnet.value,dns1:dns1.value};const r=await fetch('/api/network',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});const j=await r.json();document.getElementById('status').textContent=j.error?j.error:'Configurazione salvata, tentativo di connessione in corso';setTimeout(async()=>{await refresh();await scanWifi();},1500)}refresh().then(scanWifi);</script></body></html>");
  return html;
}

String buildWifiScanJson() {
  JsonDocument doc;
  JsonArray networks = doc.to<JsonArray>();
  const int networkCount = WiFi.scanNetworks(false, true);

  for (int i = 0; i < networkCount; ++i) {
    JsonObject entry = networks.add<JsonObject>();
    entry["ssid"] = WiFi.SSID(i);
    entry["rssi"] = WiFi.RSSI(i);
    entry["channel"] = WiFi.channel(i);
    entry["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }

  WiFi.scanDelete();

  String payload;
  serializeJson(doc, payload);
  return payload;
}

void handleStatus() {
  server.send(200, "application/json", buildStatusJson());
}

void handleWifiScan() {
  server.send(200, "application/json", buildWifiScanJson());
}

void handleNetworkGet() {
  JsonDocument doc;
  appendStatusJson(doc);
  String payload;
  serializeJson(doc["network"], payload);
  server.send(200, "application/json", payload);
}

void handleNetworkPost() {
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"JSON non valido\"}");
    return;
  }

  String message;
  if (!applyNetworkJson(doc, message)) {
    server.send(400, "application/json", String("{\"error\":\"") + message + "\"}");
    return;
  }

  server.send(200, "application/json", buildStatusJson());
}

void handleSettingsPage() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.send(200, "text/html", buildSettingsHtml());
}

void handleConfigPost() {
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"JSON non valido\"}");
    return;
  }

  String message;
  if (!applyJsonConfig(doc, message)) {
    String rawJson;
    JsonDocument rawDoc;
    rawDoc.set(server.arg("plain"));
    serializeJson(rawDoc, rawJson);
    String payload = String("{\"error\":\"") + message + "\",\"raw\":" + rawJson + "}";
    server.send(400, "application/json", payload);
    return;
  }

  server.send(200, "application/json", buildStatusJson());
}

void handleBaseTune() {
  applyBasePidTuning();
  savePreferences();
  syncBleCharacteristics(true);
  server.send(200, "application/json", buildStatusJson());
}

void handlePresetSave() {
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error || !doc["name"].is<const char*>()) {
    server.send(400, "application/json", "{\"error\":\"Preset non valido\"}");
    return;
  }

  const String presetName = doc["name"].as<const char*>();
  if (!storePresetByName(presetName)) {
    server.send(404, "application/json", "{\"error\":\"Preset non trovato\"}");
    return;
  }

  server.send(200, "application/json", buildStatusJson());
}

void handlePresetApply() {
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error || !doc["name"].is<const char*>()) {
    server.send(400, "application/json", "{\"error\":\"Preset non valido\"}");
    return;
  }

  const String presetName = doc["name"].as<const char*>();
  if (!applyPresetByName(presetName)) {
    server.send(404, "application/json", "{\"error\":\"Preset non trovato\"}");
    return;
  }

  syncBleCharacteristics(true);
  server.send(200, "application/json", buildStatusJson());
}

void handlePresets() {
  JsonDocument doc;
  appendStatusJson(doc);
  String payload;
  serializeJson(doc["presets"], payload);
  server.send(200, "application/json", payload);
}

void handleAstroStatus() {
  JsonDocument doc;
  doc["cooler_on"] = controlConfig.enabled;
  doc["temperature_c"] = sensorState.coldC;
  doc["setpoint_c"] = controlConfig.targetC;
  doc["effective_setpoint_c"] = controlState.effectiveTargetC;
  doc["cooler_power_pct"] = controlState.pwmDuty * 100.0f;
  doc["heat_sink_c"] = sensorState.hotC;
  doc["ambient_c"] = sensorState.ambientC;
  doc["humidity_pct"] = sensorState.humidityPct;
  doc["dew_point_c"] = sensorState.dewPointC;
  doc["current_a"] = sensorState.currentA;
  doc["dew_clamp_active"] = controlState.dewClampActive;
  doc["hot_protection_active"] = controlState.hotProtectionActive;
  String payload;
  serializeJson(doc, payload);
  server.send(200, "application/json", payload);
}

void handleAstroCooler() {
  if (server.method() == HTTP_GET) {
    JsonDocument doc;
    doc["enabled"] = controlConfig.enabled;
    doc["power_pct"] = controlState.pwmDuty * 100.0f;
    String payload;
    serializeJson(doc, payload);
    server.send(200, "application/json", payload);
    return;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error || !doc["enabled"].is<bool>()) {
    server.send(400, "application/json", "{\"error\":\"Parametro enabled mancante\"}");
    return;
  }

  controlConfig.enabled = doc["enabled"].as<bool>();
  savePreferences();
  syncBleCharacteristics(true);
  handleAstroStatus();
}

void handleAstroSetpoint() {
  if (server.method() == HTTP_GET) {
    JsonDocument doc;
    doc["target_c"] = controlConfig.targetC;
    doc["effective_target_c"] = controlState.effectiveTargetC;
    String payload;
    serializeJson(doc, payload);
    server.send(200, "application/json", payload);
    return;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error || !(doc["target_c"].is<float>() || doc["target_c"].is<int>())) {
    server.send(400, "application/json", "{\"error\":\"Parametro target_c mancante\"}");
    return;
  }

  controlConfig.targetC = doc["target_c"].as<float>();
  savePreferences();
  syncBleCharacteristics(true);
  handleAstroStatus();
}

void handleAstroPower() {
  JsonDocument doc;
  doc["cooler_power_pct"] = controlState.pwmDuty * 100.0f;
  doc["current_a"] = sensorState.currentA;
  doc["bus_voltage_v"] = sensorState.busVoltageV;
  String payload;
  serializeJson(doc, payload);
  server.send(200, "application/json", payload);
}

void handleAstroTemperature() {
  JsonDocument doc;
  doc["cold_c"] = sensorState.coldC;
  doc["heat_sink_c"] = sensorState.hotC;
  doc["ambient_c"] = sensorState.ambientC;
  doc["dew_point_c"] = sensorState.dewPointC;
  String payload;
  serializeJson(doc, payload);
  server.send(200, "application/json", payload);
}

void handleRoot() {
  if (captivePortalActive && !WiFi.isConnected()) {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.send(200, "text/html", buildPortalHtml());
    return;
  }

  String html;
  html.reserve(15000);
  html += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
#if THEME_RETRO_AMBER
  html += F("<title>ASI585MC Cooler</title><style>@import url('https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500;600&display=swap');body{margin:0;font-family:'IBM Plex Mono',monospace;font-size:13px;background:#140b00;color:#ffb347;text-shadow:0 0 6px rgba(255,140,0,.18);background-image:linear-gradient(rgba(255,170,0,.03) 50%,rgba(0,0,0,0) 50%);background-size:100% 4px}"
            "body:before{content:'';position:fixed;inset:0;pointer-events:none;background:radial-gradient(circle at center,rgba(255,180,60,.06),rgba(0,0,0,.25) 70%)}"
            ".wrap{max-width:1100px;margin:auto;padding:20px}.hero{display:flex;justify-content:space-between;gap:16px;align-items:center;margin-bottom:18px}"
            ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:16px}.card{background:#120900;border:1px solid #ff9f1a;border-radius:12px;padding:16px;box-shadow:0 0 0 1px rgba(255,170,0,.15) inset,0 0 22px rgba(255,140,0,.08);display:flex;flex-direction:column}.wide{grid-column:1/-1}"
            "h1,h2{margin:0 0 10px;letter-spacing:.01em;text-transform:uppercase}h1{font-size:26px}h2{font-size:18px}label{display:block;font-size:12px;color:#ffbf66;margin:8px 0 5px}input,select{width:100%;box-sizing:border-box;background:#1a0d00;color:#ffbf66;border:1px solid #ff9f1a;border-radius:10px;padding:9px;font-family:'IBM Plex Mono',monospace;font-size:12px}"
            ".uf{position:relative}.uf input{text-align:right;padding-right:44px}.uf span{position:absolute;right:10px;top:50%;transform:translateY(-50%);font-size:12px;color:#ffbf66;pointer-events:none}"
            ".actions{display:flex;justify-content:space-between;align-items:flex-end;gap:12px;margin-top:auto;padding-top:12px}.actions .right{text-align:right}"
            "button{background:#261200;color:#ffb347;border:1px solid #ff9f1a;border-radius:10px;padding:9px 12px;cursor:pointer;margin:6px 6px 0 0;font-family:'IBM Plex Mono',monospace;font-size:12px;text-transform:uppercase}button.alt{background:#1d1400}button.warn{background:#2a0d00;color:#ff8c42}button:hover{filter:brightness(1.12)}"
            ".statusGrid{display:grid;grid-template-columns:repeat(6,minmax(0,1fr));gap:10px}.statusItem{background:#1a0d00;border:1px solid #ff9f1a;border-radius:10px;padding:12px}.statusItem .metric{font-size:28px}.metric{font-size:24px;font-weight:bold}.meta{color:#ffbf66;font-size:12px}.mini{font-size:11px;line-height:1.45;color:#ffc977}.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.diag{display:grid;grid-template-columns:1fr 1fr;gap:5px 10px}.diag div:nth-child(odd){color:#ffbf66}.diag div:nth-child(even){text-align:right}.debugDiag{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:6px 12px}.debugPair{display:flex;justify-content:space-between;gap:10px;border-bottom:1px solid rgba(255,159,26,.22);padding:2px 0}.debugPair span:first-child{color:#ffbf66}.debugPair span:last-child{text-align:right;color:#ffc977}"
            ".chartWrap{position:relative;background:#1a0d00;border:1px solid #ff9f1a;border-radius:10px;padding:10px}.chartLegend{display:flex;flex-wrap:wrap;gap:14px;margin:0 0 8px;font-size:11px;color:#ffc977}.legendItem{display:flex;align-items:center;gap:6px;padding:0;border:none;background:transparent}.chartControls{margin-left:auto;display:flex;align-items:center;gap:6px}.chartControls select{width:auto;padding:4px 8px}.sw{width:14px;height:3px;border-radius:999px}.chartWrap canvas{width:100%;height:240px;display:block;image-rendering:pixelated}"
            "a{color:#ffd08a}::selection{background:#ff9f1a;color:#1a0d00}@media(max-width:640px){.row{grid-template-columns:1fr}.hero{flex-direction:column;align-items:flex-start}}</style></head><body>");
#else
  html += F("<title>ASI585MC Cooler</title><style>body{margin:0;font-family:Arial,sans-serif;background:#08111f;color:#e8f0ff}"
            ".wrap{max-width:1100px;margin:auto;padding:20px}.hero{display:flex;justify-content:space-between;gap:16px;align-items:center;margin-bottom:18px}"
            ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:16px}.card{background:#111d32;border:1px solid #243757;border-radius:16px;padding:16px;box-shadow:0 10px 30px rgba(0,0,0,.25);display:flex;flex-direction:column}.wide{grid-column:1/-1}"
            "h1,h2{margin:0 0 12px}label{display:block;font-size:13px;color:#9fb5d8;margin:10px 0 6px}input,select{width:100%;box-sizing:border-box;background:#091221;color:#e8f0ff;border:1px solid #30496e;border-radius:10px;padding:10px}"
            ".uf{position:relative}.uf input{text-align:right;padding-right:48px}.uf span{position:absolute;right:12px;top:50%;transform:translateY(-50%);font-size:13px;color:#9fb5d8;pointer-events:none}"
            ".actions{display:flex;justify-content:space-between;align-items:flex-end;gap:12px;margin-top:auto;padding-top:12px}.actions .right{text-align:right}"
            "button{background:#3385ff;color:white;border:none;border-radius:10px;padding:10px 14px;cursor:pointer;margin:6px 6px 0 0}button.alt{background:#223754}button.warn{background:#b84f32}"
            ".statusGrid{display:grid;grid-template-columns:repeat(6,minmax(0,1fr));gap:12px}.statusItem{background:#091221;border:1px solid #243757;border-radius:12px;padding:14px}.statusItem .metric{font-size:32px}.metric{font-size:28px;font-weight:bold}.meta{color:#9fb5d8;font-size:13px}.mini{font-size:12px;line-height:1.5;color:#b7c8e6}.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.diag{display:grid;grid-template-columns:1fr 1fr;gap:6px 12px}.diag div:nth-child(odd){color:#9fb5d8}.diag div:nth-child(even){text-align:right}.debugDiag{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:6px 12px}.debugPair{display:flex;justify-content:space-between;gap:10px;border-bottom:1px solid rgba(159,181,216,.22);padding:2px 0}.debugPair span:first-child{color:#9fb5d8}.debugPair span:last-child{text-align:right;color:#e8f0ff}.chartWrap{position:relative;background:#091221;border:1px solid #243757;border-radius:12px;padding:10px}.chartLegend{display:flex;flex-wrap:wrap;gap:12px;margin:0 0 8px;font-size:12px;color:#b7c8e6}.legendItem{display:flex;align-items:center;gap:6px;padding:4px 8px;background:#0f1a2d;border:1px solid #243757;border-radius:999px}.chartControls{margin-left:auto;display:flex;align-items:center;gap:6px}.chartControls select{width:auto;padding:6px 8px}.sw{width:14px;height:3px;border-radius:999px}.tooltip{position:absolute;display:none;background:rgba(8,17,31,.94);border:1px solid #35507a;border-radius:10px;padding:8px 10px;font-size:12px;color:#e8f0ff;pointer-events:none;white-space:pre-line}.chartWrap canvas{width:100%;height:240px;display:block}"
            "@media(max-width:640px){.row{grid-template-columns:1fr}.hero{flex-direction:column;align-items:flex-start}}</style></head><body>");
#endif
  html += F("<div class='wrap'><div class='hero'><div><h1>ZWO ASI Smart Cooler</h1><div class='meta'>Firmware <span id='fwVersion'>--</span> | <a id='projectLink' href='#' target='_blank' style='color:#8fc7ff'>https://github.com/naamah75/asi-smart-cooler-diy</a></div></div><div><button onclick='refreshStatus()'>Aggiorna</button></div></div>");
  html += F("<div class='grid'><div class='card wide'><h2>Stato</h2><div class='statusGrid'><div class='statusItem' title='Temperatura misurata sul lato freddo vicino all interfaccia termica della camera'><div class='metric' id='coldMetric'>--</div><div class='meta'>Lato freddo</div></div><div class='statusItem hotOnly' title='Temperatura misurata sul dissipatore lato caldo'><div class='metric' id='hotMetric'>--</div><div class='meta'>Lato caldo</div></div><div class='statusItem' title='Temperatura dell aria ambiente letta dal sensore ambiente I2C configurato'><div class='metric' id='ambientMetric'>--</div><div class='meta'>Ambiente</div></div><div class='statusItem' title='Punto di rugiada calcolato da temperatura e umidita ambiente'><div class='metric' id='dewMetric'>--</div><div class='meta'>Dew point</div></div><div class='statusItem' title='Corrente assorbita dalla cella di Peltier misurata dal sensore di corrente configurato'><div class='metric' id='currentMetric'>--</div><div class='meta'>Corrente</div></div><div class='statusItem' title='Duty PWM attualmente applicato al MOSFET che pilota la Peltier'><div class='metric' id='dutyMetric'>--</div><div class='meta'>Duty PWM</div></div></div></div>");
  html += F("<div class='card wide'><h2>Grafici</h2><div class='chartWrap'><div class='chartLegend'><div class='legendItem'><span class='sw' style='background:#55d6ff'></span><span>Lato freddo</span></div><div class='legendItem hotOnly'><span class='sw' style='background:#ff8f6b'></span><span>Lato caldo</span></div><div class='legendItem'><span class='sw' style='background:#6ee7b7'></span><span>Ambiente</span></div><div class='legendItem'><span class='sw' style='background:#3a86ff'></span><span>Potenza Peltier</span></div><div class='chartControls'><span>Scala</span><select id='chart_window'><option value='900'>15 min</option><option value='3600'>1 ora</option><option value='14400' selected>4 ore</option><option value='43200'>12 ore</option></select></div></div><canvas id='trendChart' width='1000' height='240'></canvas></div></div>");
  html += F("<div class='card'><h2>Controllo</h2><div class='row'><div><label title='Setpoint richiesto per il lato freddo'>Target freddo</label><div class='uf'><input id='target_c' type='number' step='0.1' title='Setpoint richiesto per il lato freddo'><span>°C</span></div></div><div><label title='Margine minimo di sicurezza sopra il dew point'>Margine dewp</label><div class='uf'><input id='dew_margin_c' type='number' step='0.1' title='Margine minimo di sicurezza sopra il dew point'><span>°C</span></div></div></div><div class='row'><div><label title='Limite massimo normalizzato del duty PWM della Peltier'>Duty max</label><div class='uf'><input id='max_duty' type='number' step='0.01' min='0' max='1' title='Limite massimo normalizzato del duty PWM della Peltier'><span>x</span></div></div><div class='hotOnly'><label title='Soglia di spegnimento per protezione lato caldo'>T. max caldo</label><div class='uf'><input id='max_hot_side_c' type='number' step='0.1' title='Soglia di spegnimento per protezione lato caldo'><span>°C</span></div></div></div><div class='row'><div><label title='Abilita o disabilita il raffreddamento in anello chiuso'>Controllo</label><select id='enabled' title='Abilita o disabilita il raffreddamento in anello chiuso'><option value='true'>Abilitato</option><option value='false'>Disabilitato</option></select></div><div></div></div><div class='actions'><div><button onclick='saveConfig()' title='Salva i parametri di controllo correnti'>Salva controllo</button></div><div class='right'><button class='warn' onclick='disableCooler()' title='Ferma immediatamente l uscita verso la Peltier'>Stop</button></div></div></div>");
  html += F("<div class='card'><h2>PID</h2><div class='row'><div><label>Kp</label><input id='kp' type='number' step='0.01'></div><div><label>Ki</label><input id='ki' type='number' step='0.01'></div></div><div class='row'><div><label>Kd</label><input id='kd' type='number' step='0.01'></div><div><label>Profilo rapido</label><select id='pid_profile'><option value='manual'>Manuale</option><option value='soft'>Soft</option><option value='normal'>Normal</option><option value='aggressive'>Aggressive</option></select></div></div><div class='actions'><div><button class='alt' onclick='applyBaseTune()'>Calibrazione PID</button></div><div class='right'><button onclick='savePid()'>Salva PID</button></div></div></div>");
  html += F("<div class='card'><h2>Calibrazioni</h2><div class='row'><div><label>Offset freddo</label><div class='uf'><input id='cold_offset_c' type='text' inputmode='decimal' placeholder='-5 .. 5'><span>°C</span></div></div><div><label>Offset caldo</label><div class='uf'><input id='hot_offset_c' type='text' inputmode='decimal' placeholder='-5 .. 5'><span>°C</span></div></div></div><div class='row'><div><label>Offset ambiente</label><div class='uf'><input id='ambient_offset_c' type='text' inputmode='decimal' placeholder='-5 .. 5'><span>°C</span></div></div><div><label>Offset umidita</label><div class='uf'><input id='humidity_offset_pct' type='text' inputmode='decimal' placeholder='-10 .. 10'><span>%</span></div></div></div><div class='row'><div><label>Offset corrente</label><div class='uf'><input id='current_offset_a' type='text' inputmode='decimal' placeholder='es. 0.00'><span>A</span></div></div><div><label>Scala corrente</label><div class='uf'><input id='current_scale' type='text' inputmode='decimal' placeholder='es. 1.00'><span>x</span></div></div></div><div class='actions'><div><button onclick='saveCalibration()'>Salva calibrazioni</button></div><div></div></div></div>");
  html += F("<div class='card'><h2>Preimpostazioni</h2><label title='Profili operativi salvati'>Preset</label><select id='preset_name' title='Profili operativi salvati'><option value='estate'>Estate</option><option value='inverno'>Inverno</option><option value='deep_cooling'>Deep cooling</option></select><div class='mini' id='presetInfo' style='margin-top:18px;display:grid;grid-template-columns:1fr auto;gap:8px 18px'><div>Target</div><div>--</div><div>Dew margin</div><div>--</div><div>Duty max</div><div>--</div></div><div class='actions'><div><button onclick='applyPreset()' title='Carica la preimpostazione selezionata nella configurazione attiva'>Applica preset</button></div><div class='right'><button class='alt' onclick='savePreset()' title='Sovrascrive la preimpostazione selezionata con la configurazione attuale'>Salva preset corrente</button></div></div></div>");
  html += F("<div class='card'><h2>Statistiche</h2><div class='mini diag'><div>WiFi signal</div><div id='wifiRssi'>--</div><div>BSSID</div><div id='wifiBssid'>--</div><div>IP</div><div id='boardIp'>--</div><div>BLE</div><div id='bleState'>--</div><div>Free RAM</div><div id='freeHeap'>--</div><div>CPU temp</div><div id='cpuTemp'>--</div><div>Avvio</div><div id='bootTime'>--</div><div>I2C scan</div><div id='i2cScan'>--</div></div></div>");
  html += F("<div class='card wide'><h2>Debug</h2><div class='row'><div><label>Override PWM manuale</label><select id='debug_manual_pwm_enabled'><option value='false'>Disabilitato</option><option value='true'>Abilitato</option></select></div><div><label>Duty manuale</label><div class='uf'><input id='debug_manual_pwm_pct' type='number' step='1' min='0' max='100'><span>%</span></div></div></div><div class='actions'><div><button class='warn' onclick='applyDebugPwm()'>Applica PWM manuale</button><button class='alt' onclick='stopDebugPwm()'>Disattiva debug PWM</button><button class='alt' onclick='resetDebugMinMax()'>Reset min/max temp</button></div><div class='right mini'>Runtime only: non salvato in flash. La protezione lato caldo resta attiva.</div></div><div class='mini diag' style='margin-top:12px'><div>PWM manuale attivo</div><div id='debugManualState'>--</div><div>Duty manuale impostato</div><div id='debugManualDuty'>--</div><div>PWM realmente applicato</div><div id='debugAppliedPwm'>--</div></div><div class='mini debugDiag' id='debugReadings' style='margin-top:14px'></div></div>");
  html += F("<script>function fmt(v,u=''){const n=Number(v);return Number.isFinite(n)?n.toFixed(2)+u:'--'}function boolVal(id){return document.getElementById(id).value==='true'}function numVal(id){const el=document.getElementById(id);if(!el)return NaN;const raw=String(el.value||'').trim().replace(',','.');return raw===''?NaN:Number(raw)}"
            "const HIST_MAX=2160;const HIST_STEP_S=20;const hist={cold:[],hot:[],ambient:[],power:[],t:[]};let lastHistS=-999;let chartGeom=null;let hotEnabled=false;let chartWindowS=14400;let suspendRefresh=false;let refreshSeq=0;const dirtyFields=new Set();function pushHistSample(j){const nowS=Math.floor((j.board.uptime_s||0));if(nowS-lastHistS<HIST_STEP_S&&hist.t.length)return;lastHistS=nowS;const map={cold:j.sensor.cold_c,hot:j.sensor.hot_c,ambient:j.sensor.ambient_c,power:j.control.pwm_duty*100,t:nowS};Object.keys(map).forEach(k=>{hist[k].push(Number(map[k]));if(hist[k].length>HIST_MAX)hist[k].shift()})}function fillField(id,v){const el=document.getElementById(id);const n=Number(v);if(el&&Number.isFinite(n)&&document.activeElement!==el&&!dirtyFields.has(id))el.value=n}function fillBool(id,v){const el=document.getElementById(id);if(el&&document.activeElement!==el&&!dirtyFields.has(id))el.value=v?'true':'false'}function markDirty(id){dirtyFields.add(id)}function clearDirty(ids){ids.forEach(id=>dirtyFields.delete(id))}function setHotVisibility(on){hotEnabled=!!on;document.querySelectorAll('.hotOnly').forEach(el=>el.style.display=hotEnabled?'':'none')}"
            "async function api(path,body){const r=await fetch(path,{method:body?'POST':'GET',headers:{'Content-Type':'application/json'},body:body?JSON.stringify(body):undefined});const data=await r.json();if(!r.ok){throw new Error((data&&data.error?data.error:('HTTP '+r.status))+(data&&data.raw?(' | raw: '+data.raw):''))}return data}"
            "function drawLine(ctx,data,minV,maxV,color,left,top,bottom,w){if(data.length<2)return;ctx.beginPath();ctx.lineWidth=2.2;ctx.strokeStyle=color;data.forEach((v,i)=>{const px=left+(i/Math.max(data.length-1,1))*w;const py=bottom-((v-minV)/Math.max(maxV-minV,0.001))*(bottom-top);if(i===0)ctx.moveTo(px,py);else ctx.lineTo(px,py)});ctx.stroke()}function redrawCharts(){const c=document.getElementById('trendChart');const x=c.getContext('2d');const left=46,right=46,top=14,bottom=c.height-26,w=c.width-left-right;chartGeom={left,right,top,bottom,w};x.clearRect(0,0,c.width,c.height);const tMin=0;const tMax=40;for(let i=0;i<5;i++){const y=top+i*((bottom-top)/4);x.strokeStyle='#223754';x.lineWidth=1;x.beginPath();x.moveTo(left,y);x.lineTo(c.width-right,y);x.stroke();const tVal=(tMax-((tMax-tMin)/4)*i).toFixed(0);x.fillStyle='#9fb5d8';x.font='11px Arial';x.fillText(tVal,4,y+4);const pVal=(100-25*i).toFixed(0);x.fillText(pVal,c.width-right+8,y+4)}for(let h=0;h<=4;h++){const px=left+(h/4)*w;x.strokeStyle='#1a2740';x.beginPath();x.moveTo(px,top);x.lineTo(px,bottom);x.stroke();x.fillStyle='#9fb5d8';x.font='11px Arial';x.fillText(`-${4-h}h`,Math.max(px-10,2),c.height-6)}x.strokeStyle='#3b4f73';x.beginPath();x.moveTo(left,top);x.lineTo(left,bottom);x.lineTo(c.width-right,bottom);x.lineTo(c.width-right,top);x.stroke();drawLine(x,hist.cold,tMin,tMax,'#55d6ff',left,top,bottom,w);if(hotEnabled){drawLine(x,hist.hot,tMin,tMax,'#ff8f6b',left,top,bottom,w)}drawLine(x,hist.ambient,tMin,tMax,'#6ee7b7',left,top,bottom,w);drawLine(x,hist.power,0,100,'#3a86ff',left,top,bottom,w)}function syncUi(j){coldMetric.textContent=fmt(j.sensor.cold_c,' °C');dewMetric.textContent=fmt(j.sensor.dew_point_c,' °C');currentMetric.textContent=fmt(j.sensor.current_a,' A');hotMetric.textContent=fmt(j.sensor.hot_c,' °C');ambientMetric.textContent=fmt(j.sensor.ambient_c,' °C');dutyMetric.textContent=fmt(j.control.pwm_duty*100,' %');fwVersion.textContent=j.board.firmware_version||'--';projectLink.href=j.board.project_url||'#';projectLink.textContent=j.board.project_url||'--';boardIp.textContent=j.network.ip||'--';wifiRssi.textContent=Number.isFinite(j.board.wifi_rssi_dbm)?j.board.wifi_rssi_dbm+' dBm':'--';wifiBssid.textContent=j.board.wifi_bssid||'--';bleState.textContent=j.board.ble_connected?('Connected '+j.board.ble_clients):'Idle';freeHeap.textContent=Number.isFinite(j.board.free_heap_bytes)?j.board.free_heap_bytes+' B':'--';cpuTemp.textContent=fmt(j.board.cpu_temp_c,' °C');bootTime.textContent=j.board.boot_time_local||'--';syncDebugUi(j);setHotVisibility(!!j.control.hot_sensor_enabled);pushHistSample(j);redrawCharts();"
            "fillField('target_c',j.control.target_c);fillField('dew_margin_c',j.control.dew_margin_c);fillField('max_duty',j.control.max_duty);fillField('max_hot_side_c',j.control.max_hot_side_c);fillBool('enabled',j.control.enabled);fillBool('hot_sensor_enabled',j.control.hot_sensor_enabled);"
            "fillField('kp',j.pid.kp);fillField('ki',j.pid.ki);fillField('kd',j.pid.kd);document.getElementById('pid_profile').value=['soft','normal','aggressive'].includes(j.pid.profile)?j.pid.profile:'manual';"
            "fillField('cold_offset_c',j.calibration.cold_offset_c);fillField('hot_offset_c',j.calibration.hot_offset_c);fillField('ambient_offset_c',j.calibration.ambient_offset_c);fillField('humidity_offset_pct',j.calibration.humidity_offset_pct);fillField('current_offset_a',j.calibration.current_offset_a);fillField('current_scale',j.calibration.current_scale);const p=document.getElementById('preset_name').value;const pr=j.presets&&j.presets[p]?j.presets[p]:null;document.getElementById('presetInfo').innerHTML=pr?`<div>Target</div><div>${fmt(pr.target_c,' °C')}</div><div>Dew margin</div><div>${fmt(pr.dew_margin_c,' °C')}</div><div>Duty max</div><div>${fmt(pr.max_duty*100,' %')}</div>`:'<div>Preset</div><div>non disponibile</div>'}"
            "function syncDebugUi(j){const en=document.getElementById('debug_manual_pwm_enabled');const pct=document.getElementById('debug_manual_pwm_pct');if(document.activeElement!==en){en.value=j.debug.manual_pwm_enabled?'true':'false'}if(document.activeElement!==pct){pct.value=Number.isFinite(Number(j.debug.manual_pwm_pct))?Number(j.debug.manual_pwm_pct).toFixed(0):0}debugManualState.textContent=j.debug.manual_pwm_enabled?'abilitato':'disabilitato';debugManualDuty.textContent=fmt(j.debug.manual_pwm_pct,' %');debugAppliedPwm.textContent=fmt(j.control.pwm_duty*100,' %');const i2c=document.getElementById('i2cScan');if(i2c){i2c.textContent=j.debug.i2c_scan||'--'}const s=j.sensor;const rows=[['NTC filtro alpha',j.debug.temperature_filter_alpha],['Campioni ADC',j.debug.adc_sample_count],['Freddo min/max',fmt(j.debug.cold_min_c,' °C')+' / '+fmt(j.debug.cold_max_c,' °C')],['Caldo min/max',fmt(j.debug.hot_min_c,' °C')+' / '+fmt(j.debug.hot_max_c,' °C')],['Freddo raw C',fmt(s.cold_raw_c,' °C')],['Freddo filtrato C',fmt(s.cold_c,' °C')],['Freddo trend',fmt(j.debug.cold_trend_c_per_min,' °C/min')],['Caldo raw C',fmt(s.hot_raw_c,' °C')],['Caldo filtrato C',fmt(s.hot_c,' °C')],['Caldo trend',fmt(j.debug.hot_trend_c_per_min,' °C/min')],['Potenza trend Peltier',fmt(j.debug.peltier_trend_power_pct,' %')],['NTC freddo raw',s.cold_ntc_raw],['NTC freddo V',fmt(s.cold_ntc_voltage_v,' V')],['NTC freddo ohm',fmt(s.cold_ntc_ohm,' ohm')],['NTC caldo raw',s.hot_ntc_raw],['NTC caldo V',fmt(s.hot_ntc_voltage_v,' V')],['NTC caldo ohm',fmt(s.hot_ntc_ohm,' ohm')],['ADC corrente raw',s.adc32_raw],['ADC corrente V',fmt(s.adc32_voltage_v,' V')],['BME280 ok',s.bme280_ok?'si':'no'],['Ambiente',fmt(s.ambient_c,' °C')],['Umidita',fmt(s.humidity_pct,' %')],['Dew point',fmt(s.dew_point_c,' °C')],['INA219 ok',s.ina219_ok?'si':'no'],['Corrente',fmt(s.current_a,' A')],['Bus Peltier',fmt(s.bus_voltage_v,' V')],['Hot protection',j.control.hot_protection_active?'attiva':'no'],['Dew clamp',j.control.dew_clamp_active?'attivo':'no']];document.getElementById('debugReadings').innerHTML=rows.map(r=>`<div class='debugPair'><span>${r[0]}</span><span>${r[1]}</span></div>`).join('')}"
            "async function refreshStatus(){if(suspendRefresh)return;const seq=++refreshSeq;try{const data=await api('/api/status');if(seq===refreshSeq&&!suspendRefresh)syncUi(data)}catch(e){console.error(e)}}"
            "async function saveConfig(){syncUi(await api('/api/config',{enabled:boolVal('enabled'),target_c:numVal('target_c'),dew_margin_c:numVal('dew_margin_c'),max_duty:numVal('max_duty'),max_hot_side_c:numVal('max_hot_side_c')}))}"
            "async function applyDebugPwm(){const en=boolVal('debug_manual_pwm_enabled');const pct=numVal('debug_manual_pwm_pct');syncUi(await api('/api/config',{manual_pwm_enabled:en,manual_pwm_pct:pct,debug:{manual_pwm_enabled:en,manual_pwm_pct:pct}}))}"
            "async function stopDebugPwm(){document.getElementById('debug_manual_pwm_enabled').value='false';document.getElementById('debug_manual_pwm_pct').value='0';syncUi(await api('/api/config',{manual_pwm_enabled:false,manual_pwm_pct:0,debug:{manual_pwm_enabled:false,manual_pwm_pct:0}}))}"
            "async function resetDebugMinMax(){syncUi(await api('/api/config',{reset_temp_minmax:true,debug:{reset_temp_minmax:true}}))}"
            "async function disableCooler(){syncUi(await api('/api/config',{enabled:false}))}"
            "async function savePid(){syncUi(await api('/api/config',{pid:{kp:numVal('kp'),ki:numVal('ki'),kd:numVal('kd')}}))}"
            "async function savePidProfile(){syncUi(await api('/api/config',{pid_profile:document.getElementById('pid_profile').value}))}"
            "async function applyBaseTune(){syncUi(await api('/api/pid/base-tune',{}))}"
            "async function saveCalibration(){const ids=['cold_offset_c','hot_offset_c','ambient_offset_c','humidity_offset_pct','current_offset_a','current_scale'];const calibration={};ids.forEach(id=>{const el=document.getElementById(id);if(!el)return;const raw=String(el.value||'').trim();if(raw!==''){const key={cold_offset_c:'cold_offset_c',hot_offset_c:'hot_offset_c',ambient_offset_c:'ambient_offset_c',humidity_offset_pct:'humidity_offset_pct',current_offset_a:'current_offset_a',current_scale:'current_scale'}[id];calibration[key]=raw}});if(!Object.keys(calibration).length){alert('Nessun valore valido da salvare');return}const requestBody={calibration};suspendRefresh=true;try{const res=await api('/api/config',requestBody);clearDirty(ids);syncUi(res)}catch(e){alert('Salvataggio calibrazioni fallito: '+e.message+' | payload: '+JSON.stringify(requestBody))}finally{suspendRefresh=false}}"
            "async function applyPreset(){syncUi(await api('/api/preset/apply',{name:document.getElementById('preset_name').value}))}"
            "async function savePreset(){syncUi(await api('/api/preset/save',{name:document.getElementById('preset_name').value}))}document.getElementById('preset_name').addEventListener('change',refreshStatus);document.getElementById('pid_profile').addEventListener('change',savePidProfile);"
            "refreshStatus();setInterval(refreshStatus,2000);</script><script>"
            "function drawLine(ctx,data,minV,maxV,color,left,top,bottom,w){if(data.length<2)return;ctx.beginPath();ctx.lineWidth=2.2;ctx.strokeStyle=color;let started=false;data.forEach((v,i)=>{if(!Number.isFinite(v)){started=false;return}const px=left+(i/Math.max(data.length-1,1))*w;const py=bottom-((v-minV)/Math.max(maxV-minV,0.001))*(bottom-top);if(!started){ctx.moveTo(px,py);started=true}else ctx.lineTo(px,py)});ctx.stroke()}"
            "function timeLabel(s){return s>=3600?Math.round(s/3600)+'h':Math.round(s/60)+'m'}function chartView(k,idx){return idx.map(i=>hist[k][i])}"
            "function redrawCharts(){const c=document.getElementById('trendChart');const x=c.getContext('2d');const left=46,right=46,top=14,bottom=c.height-26,w=c.width-left-right;chartGeom={left,right,top,bottom,w};x.clearRect(0,0,c.width,c.height);const latest=hist.t.length?hist.t[hist.t.length-1]:0;const from=latest-chartWindowS;const idx=hist.t.map((t,i)=>t>=from?i:-1).filter(i=>i>=0);const cold=chartView('cold',idx),hot=chartView('hot',idx),ambient=chartView('ambient',idx),power=chartView('power',idx);const tMin=0;const tMax=40;for(let i=0;i<5;i++){const y=top+i*((bottom-top)/4);x.strokeStyle='#223754';x.lineWidth=1;x.beginPath();x.moveTo(left,y);x.lineTo(c.width-right,y);x.stroke();const tVal=(tMax-((tMax-tMin)/4)*i).toFixed(0);x.fillStyle='#9fb5d8';x.font='11px Arial';x.fillText(tVal,4,y+4);const pVal=(100-25*i).toFixed(0);x.fillText(pVal,c.width-right+8,y+4)}for(let h=0;h<=4;h++){const px=left+(h/4)*w;x.strokeStyle='#1a2740';x.beginPath();x.moveTo(px,top);x.lineTo(px,bottom);x.stroke();x.fillStyle='#9fb5d8';x.font='11px Arial';x.fillText('-'+timeLabel(chartWindowS*(1-h/4)),Math.max(px-14,2),c.height-6)}x.strokeStyle='#3b4f73';x.beginPath();x.moveTo(left,top);x.lineTo(left,bottom);x.lineTo(c.width-right,bottom);x.lineTo(c.width-right,top);x.stroke();drawLine(x,cold,tMin,tMax,'#55d6ff',left,top,bottom,w);if(hotEnabled){drawLine(x,hot,tMin,tMax,'#ff8f6b',left,top,bottom,w)}drawLine(x,ambient,tMin,tMax,'#6ee7b7',left,top,bottom,w);drawLine(x,power,0,100,'#3a86ff',left,top,bottom,w)}"
            "document.getElementById('chart_window').addEventListener('change',e=>{chartWindowS=Number(e.target.value);redrawCharts();refreshStatus()});['cold_offset_c','hot_offset_c','ambient_offset_c','humidity_offset_pct','current_offset_a','current_scale','target_c','dew_margin_c','max_duty','max_hot_side_c','kp','ki','kd','debug_manual_pwm_pct'].forEach(id=>{const el=document.getElementById(id);if(el){el.addEventListener('input',()=>markDirty(id))}});['enabled','hot_sensor_enabled','pid_profile','preset_name','debug_manual_pwm_enabled'].forEach(id=>{const el=document.getElementById(id);if(el){el.addEventListener('change',()=>markDirty(id))}});</script></div></body></html>");
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.send(200, "text/html", html);
}

void handleCaptiveRedirect() {
  if (captivePortalActive && !WiFi.isConnected() && server.method() == HTTP_GET) {
    server.sendHeader("Location", String("http://") + CAPTIVE_IP.toString(), true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "Not found");
}

#if ENABLE_BLE
class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic) override {
    const std::string value = characteristic->getValue();
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, value.c_str());
    if (error) {
      return;
    }

    String message;
    if (applyJsonConfig(doc, message)) {
      bleStatusCharacteristic->setValue(buildStatusJson().c_str());
      bleStatusCharacteristic->notify();
    }
  }
};

class StructuredEnabledCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic) override {
    bool enabled = false;
    if (!decodeBleBool(characteristic->getValue(), enabled)) {
      return;
    }
    controlConfig.enabled = enabled;
    savePreferences();
    syncBleCharacteristics(true);
  }
};

class StructuredTargetCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic) override {
    int16_t targetCenti = 0;
    if (!decodeBleInt16(characteristic->getValue(), targetCenti)) {
      return;
    }
    controlConfig.targetC = static_cast<float>(targetCenti) / 100.0f;
    savePreferences();
    syncBleCharacteristics(true);
  }
};

class StructuredPresetCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic) override {
    const String command = String(characteristic->getValue().c_str());
    if (command.startsWith("apply:")) {
      applyPresetByName(command.substring(6));
    } else if (command.startsWith("save:")) {
      storePresetByName(command.substring(5));
    } else {
      return;
    }
    syncBleCharacteristics(true);
  }
};

class StructuredTuneCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic) override {
    const String command = String(characteristic->getValue().c_str());
    if (command == "base") {
      applyBasePidTuning();
    } else {
      applyPidProfile(command);
    }
    savePreferences();
    syncBleCharacteristics(true);
  }
};

void setupBle() {
  NimBLEDevice::init(networkConfig.hostname.c_str());
  bleServer = NimBLEDevice::createServer();
  NimBLEService* service = bleServer->createService(BLE_SERVICE_UUID);
  NimBLEService* structuredService = bleServer->createService(BLE_STRUCTURED_SERVICE_UUID);

  bleStatusCharacteristic = service->createCharacteristic(
      BLE_STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  bleStatusCharacteristic->setValue("{}");

  bleCommandCharacteristic = service->createCharacteristic(
      BLE_COMMAND_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  bleCommandCharacteristic->setCallbacks(new CommandCallbacks());

  bleTelemetryCharacteristic = structuredService->createCharacteristic(
      BLE_TELEMETRY_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  bleEnabledCharacteristic = structuredService->createCharacteristic(
      BLE_ENABLE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  bleEnabledCharacteristic->setCallbacks(new StructuredEnabledCallbacks());
  bleTargetCharacteristic = structuredService->createCharacteristic(
      BLE_TARGET_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  bleTargetCharacteristic->setCallbacks(new StructuredTargetCallbacks());
  blePresetCharacteristic = structuredService->createCharacteristic(
      BLE_PRESET_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  blePresetCharacteristic->setCallbacks(new StructuredPresetCallbacks());
  bleTuneCharacteristic = structuredService->createCharacteristic(
      BLE_TUNE_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  bleTuneCharacteristic->setCallbacks(new StructuredTuneCallbacks());

  service->start();
  structuredService->start();
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->addServiceUUID(BLE_STRUCTURED_SERVICE_UUID);
  advertising->start();
  syncBleCharacteristics(false);
}
#else
void setupBle() {}
#endif

void startCaptivePortal() {
  if (captivePortalActive) {
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(CAPTIVE_IP, CAPTIVE_IP, IPAddress(255, 255, 255, 0));
  const String apSsid = networkConfig.hostname + "-setup";
  WiFi.softAP(apSsid.c_str());
  dnsServer.start(DNS_PORT, "*", CAPTIVE_IP);
  captivePortalActive = true;
  Serial.printf("Captive portal attivo su SSID '%s' IP %s\n", apSsid.c_str(), CAPTIVE_IP.toString().c_str());
}

void stopCaptivePortal() {
  if (!captivePortalActive) {
    return;
  }

  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  captivePortalActive = false;
}

void reconnectWifi() {
  stopCaptivePortal();
  if (mdnsStarted) {
    MDNS.end();
    mdnsStarted = false;
  }
  WiFi.disconnect(true, true);
  lastWifiAttemptMs = 0;
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/settings", HTTP_GET, handleSettingsPage);
  server.on("/generate_204", HTTP_GET, handleRoot);
  server.on("/hotspot-detect.html", HTTP_GET, handleRoot);
  server.on("/connecttest.txt", HTTP_GET, handleRoot);
  server.on("/ncsi.txt", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/network", HTTP_GET, handleNetworkGet);
  server.on("/api/network", HTTP_POST, handleNetworkPost);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/presets", HTTP_GET, handlePresets);
  server.on("/api/preset/save", HTTP_POST, handlePresetSave);
  server.on("/api/preset/apply", HTTP_POST, handlePresetApply);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/pid/base-tune", HTTP_POST, handleBaseTune);
  server.on("/api/astro/status", HTTP_GET, handleAstroStatus);
  server.on("/api/astro/cooler", HTTP_GET, handleAstroCooler);
  server.on("/api/astro/cooler", HTTP_POST, handleAstroCooler);
  server.on("/api/astro/setpoint", HTTP_GET, handleAstroSetpoint);
  server.on("/api/astro/setpoint", HTTP_POST, handleAstroSetpoint);
  server.on("/api/astro/power", HTTP_GET, handleAstroPower);
  server.on("/api/astro/temperature", HTTP_GET, handleAstroTemperature);
  server.onNotFound(handleCaptiveRedirect);
  server.begin();
}

void ensureWifi() {
  if (!hasWifiCredentials()) {
    startCaptivePortal();
    return;
  }

  if (WiFi.isConnected()) {
    stopCaptivePortal();
    if (!wifiConnectedLogged) {
      Serial.printf(
          "WiFi connesso: SSID='%s' IP=%s gateway=%s mDNS=http://%s.local/\n",
          WiFi.SSID().c_str(),
          WiFi.localIP().toString().c_str(),
          WiFi.gatewayIP().toString().c_str(),
          networkConfig.hostname.c_str());
      wifiConnectedLogged = true;
    }
    if (!ntpConfigured) {
      configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
      ntpConfigured = true;
    }
    if (bootEpoch == 0) {
      const time_t now = time(nullptr);
      if (now > 1700000000) {
        bootEpoch = now - (millis() / 1000UL);
      }
    }
    if (!mdnsStarted && MDNS.begin(networkConfig.hostname.c_str())) {
      MDNS.addService("http", "tcp", 80);
      MDNS.addServiceTxt("http", "tcp", "path", "/");
      MDNS.addServiceTxt("http", "tcp", "api", "/api/status");
      MDNS.addServiceTxt("http", "tcp", "fw", FW_VERSION);
      MDNS.addServiceTxt("http", "tcp", "device", "asi585mc-cooler");
      mdnsStarted = true;
      Serial.printf("mDNS attivo su http://%s.local\n", networkConfig.hostname.c_str());
    }
    return;
  }

  startCaptivePortal();

  if (mdnsStarted) {
    MDNS.end();
    mdnsStarted = false;
  }
  wifiConnectedLogged = false;

  const uint32_t now = millis();
  if (now - lastWifiAttemptMs < timing::WIFI_RETRY_MS) {
    return;
  }

  lastWifiAttemptMs = now;
  WiFi.mode(captivePortalActive ? WIFI_AP_STA : WIFI_STA);
  if (!networkConfig.dhcpEnabled) {
    WiFi.config(networkConfig.staticIp, networkConfig.gateway, networkConfig.subnet, networkConfig.dns1);
  }
  WiFi.setHostname(networkConfig.hostname.c_str());
  WiFi.begin(networkConfig.wifiSsid.c_str(), networkConfig.wifiPassword.c_str());
}

void updateStatusLed(const uint32_t now) {
  if (WiFi.isConnected()) {
    writeStatusLed(true);
    return;
  }

  if (now - lastLedToggleMs >= 500) {
    lastLedToggleMs = now;
    writeStatusLed(!statusLedOn);
  }
}

void updateOledDisplay() {
#if BOARD_LOLIN_S2_PICO
  static uint32_t lastDisplayMs = 0;
  const uint32_t now = millis();
  if (now - lastDisplayMs < 1000) {
    return;
  }
  lastDisplayMs = now;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(WiFi.isConnected() ? WiFi.localIP().toString() : String("WiFi..."));
  display.setCursor(0, 10);
  display.print("Cold ");
  if (isfinite(sensorState.coldC)) {
    display.print(sensorState.coldC, 1);
    display.print("C");
  } else {
    display.print("--");
  }
  display.setCursor(70, 10);
  display.print("PWM ");
  display.print(controlState.pwmDuty * 100.0f, 0);
  display.print("%");
  display.setCursor(0, 22);
  display.print(controlConfig.enabled ? "ON " : "OFF");
  display.print(" T ");
  display.print(controlConfig.targetC, 1);
  display.print("C");
  display.display();
#endif
}

void setupSensors() {
  Wire.begin(pins::I2C_SDA, pins::I2C_SCL);
#if BOARD_LOLIN_S2_PICO
  pinMode(pins::OLED_RESET, OUTPUT);
  digitalWrite(pins::OLED_RESET, LOW);
  delay(10);
  digitalWrite(pins::OLED_RESET, HIGH);
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("ASI cooler boot");
    display.display();
  }
#endif
  Serial.printf("Pin NTC: freddo=GPIO%u caldo=GPIO%u\n", pins::COLD_NTC_ADC, pins::HOT_NTC_ADC);
  scanI2cBus();
#if USE_SHT30
  ambientSensorPresent = ambientSensor.begin(0x44) || ambientSensor.begin(0x45);
#else
  ambientSensorPresent = bme.begin(0x76) || bme.begin(0x77);
#endif
  sensorState.ambientSensorOk = ambientSensorPresent;
  sensorState.bmeOk = ambientSensorPresent;
  Serial.printf("Sensore ambiente: %s %s\n", USE_SHT30 ? "SHT30" : "BME280", ambientSensorPresent ? "OK" : "non trovato");

#if USE_ADS1115
  sensorState.adsOk = ads.begin();
  if (sensorState.adsOk) {
    ads.setGain(GAIN_ONE);
  }
#else
  sensorState.adsOk = false;
#endif

#if USE_ACS71X_CURRENT
  sensorState.inaOk = false;
#else
  ina219Present = ina219.begin();
  sensorState.inaOk = ina219Present;
  if (ina219Present) {
    ina219.setCalibration_32V_2A();
  }
#endif

#if !USE_ADS1115
  analogReadResolution(12);
  pinMode(pins::CURRENT_ADC, INPUT);
  pinMode(pins::COLD_NTC_ADC, INPUT);
  pinMode(pins::HOT_NTC_ADC, INPUT);
  analogSetPinAttenuation(pins::CURRENT_ADC, ADC_11db);
  analogSetPinAttenuation(pins::COLD_NTC_ADC, ADC_11db);
  analogSetPinAttenuation(pins::HOT_NTC_ADC, ADC_11db);
#if CONFIG_IDF_TARGET_ESP32
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
#endif
#if USE_ACS71X_CURRENT
  analogSetPinAttenuation(pins::CURRENT_ADC, ADC_11db);
#endif
#endif
}

void setupControl() {
  ledcSetup(pwm::CHANNEL, pwm::FREQUENCY_HZ, pwm::RESOLUTION_BITS);
  ledcAttachPin(pins::PELTIER_PWM, pwm::CHANNEL);
  setPwmDuty(0.0f);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(pins::STATUS_LED, OUTPUT);
  writeStatusLed(false);
  loadPreferences();
  setupControl();
  setupSensors();
  ensureWifi();
  setupWebServer();
  setupBle();
  refreshSensors();
  syncBleCharacteristics(false);
  Serial.println("ASI585MC cooler controller avviato");
}

void loop() {
  const uint32_t now = millis();
  ensureWifi();
  updateStatusLed(now);
  updateOledDisplay();
  if (captivePortalActive) {
    dnsServer.processNextRequest();
  }
  server.handleClient();

  if (now - lastSensorMs >= timing::SENSOR_MS) {
    lastSensorMs = now;
    refreshSensors();
  }

  if (now - lastControlMs >= timing::CONTROL_MS) {
    const float dtSeconds = lastControlMs == 0 ? (timing::CONTROL_MS / 1000.0f)
                                               : (now - lastControlMs) / 1000.0f;
    lastControlMs = now;
    updateControl(max(dtSeconds, 0.1f));
  }

#if ENABLE_BLE
  if (bleStatusCharacteristic != nullptr && now - lastBleNotifyMs >= timing::STATUS_NOTIFY_MS) {
    lastBleNotifyMs = now;
    syncBleCharacteristics(true);
  }
#endif

  if (now - lastSerialLogMs >= timing::SERIAL_LOG_MS) {
    lastSerialLogMs = now;
    logStatusToSerial();
  }
}
