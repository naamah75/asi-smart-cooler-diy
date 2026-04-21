#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_BME280.h>
#include <Adafruit_INA219.h>
#include <DNSServer.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

namespace pins {
constexpr uint8_t PELTIER_PWM = 25;
constexpr uint8_t COLD_NTC_ADC = 34;
constexpr uint8_t HOT_NTC_ADC = 35;
constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;
}  // namespace pins

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
  float coldOffsetC = 0.0f;
  float hotOffsetC = 0.0f;
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
  float maxDuty = 0.90f;
  float maxHotSideC = 55.0f;
};

struct ThermistorConfig {
  float seriesResistorOhm = 10000.0f;
  float nominalResistanceOhm = 10000.0f;
  float nominalTempC = 25.0f;
  float beta = 3950.0f;
  float supplyVoltage = 3.3f;
};

struct PresetConfig {
  float targetC = 5.0f;
  float dewMarginC = 2.0f;
  float maxDuty = 0.90f;
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
  float ambientC = NAN;
  float humidityPct = NAN;
  float dewPointC = NAN;
  float currentA = NAN;
  float busVoltageV = NAN;
  bool bmeOk = false;
  bool inaOk = false;
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

Calibration calibration;
PidConfig pidConfig;
ControlConfig controlConfig;
ThermistorConfig thermistorConfig;
NetworkConfig networkConfig;
PresetConfig presetEstate;
PresetConfig presetInverno;
PresetConfig presetDeepCooling;
SensorState sensorState;
ControlState controlState;

Preferences preferences;
Adafruit_BME280 bme;
Adafruit_INA219 ina219;
WebServer server(80);
DNSServer dnsServer;
NimBLEServer* bleServer = nullptr;
NimBLECharacteristic* bleStatusCharacteristic = nullptr;
NimBLECharacteristic* bleCommandCharacteristic = nullptr;
NimBLECharacteristic* bleTelemetryCharacteristic = nullptr;
NimBLECharacteristic* bleEnabledCharacteristic = nullptr;
NimBLECharacteristic* bleTargetCharacteristic = nullptr;
NimBLECharacteristic* blePresetCharacteristic = nullptr;
NimBLECharacteristic* bleTuneCharacteristic = nullptr;

uint32_t lastSensorMs = 0;
uint32_t lastControlMs = 0;
uint32_t lastWifiAttemptMs = 0;
uint32_t lastBleNotifyMs = 0;
uint32_t lastSerialLogMs = 0;
float pidIntegral = 0.0f;
float pidLastError = 0.0f;
bool pidHasHistory = false;
bool mdnsStarted = false;
bool captivePortalActive = false;
bool ntpConfigured = false;
time_t bootEpoch = 0;

constexpr char PREF_NAMESPACE[] = "cooler";
constexpr char BLE_SERVICE_UUID[] = "9b220100-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_STATUS_UUID[] = "9b220101-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_COMMAND_UUID[] = "9b220102-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_STRUCTURED_SERVICE_UUID[] = "9b220200-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_TELEMETRY_UUID[] = "9b220201-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_ENABLE_UUID[] = "9b220202-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_TARGET_UUID[] = "9b220203-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_PRESET_UUID[] = "9b220204-35ac-4e4e-9f6f-8ed48fc5c001";
constexpr char BLE_TUNE_UUID[] = "9b220205-35ac-4e4e-9f6f-8ed48fc5c001";
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

float readThermistorTemperatureC(const uint8_t pin) {
  constexpr size_t sampleCount = 16;
  uint32_t mvSum = 0;

  for (size_t i = 0; i < sampleCount; ++i) {
    mvSum += analogReadMilliVolts(pin);
    delay(2);
  }

  const float millivolts = static_cast<float>(mvSum) / static_cast<float>(sampleCount);
  const float voltage = millivolts / 1000.0f;
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

void setPwmDuty(const float duty) {
  controlState.pwmDuty = clampf(duty, 0.0f, controlConfig.maxDuty);
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
  presetDeepCooling.maxDuty = 0.90f;
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
  const float coldNtcC = readThermistorTemperatureC(pins::COLD_NTC_ADC);
  sensorState.coldNtcOk = !isnan(coldNtcC);
  sensorState.coldC = sensorState.coldNtcOk ? coldNtcC + calibration.coldOffsetC : NAN;

  const float hotNtcC = readThermistorTemperatureC(pins::HOT_NTC_ADC);
  sensorState.hotNtcOk = !isnan(hotNtcC);
  sensorState.hotC = sensorState.hotNtcOk ? hotNtcC + calibration.hotOffsetC : NAN;

  const float ambientC = bme.readTemperature();
  const float humidity = bme.readHumidity();
  sensorState.bmeOk = isfinite(ambientC) && isfinite(humidity) && humidity >= 0.0f && humidity <= 100.0f;
  sensorState.ambientC = sensorState.bmeOk ? ambientC + calibration.ambientTempOffsetC : NAN;
  sensorState.humidityPct = sensorState.bmeOk ? clampf(humidity + calibration.humidityOffsetPct, 0.0f, 100.0f) : NAN;
  sensorState.dewPointC = sensorState.bmeOk ? computeDewPointC(sensorState.ambientC, sensorState.humidityPct) : NAN;

  const float currentA = ina219.getCurrent_mA() / 1000.0f;
  const float busVoltage = ina219.getBusVoltage_V();
  sensorState.inaOk = isfinite(currentA) && isfinite(busVoltage);
  sensorState.currentA = sensorState.inaOk ? (currentA * calibration.currentScale) + calibration.currentOffsetA : NAN;
  sensorState.busVoltageV = sensorState.inaOk ? busVoltage : NAN;
}

void updateControl(const float dtSeconds) {
  controlState.hotProtectionActive = false;

  if (!controlConfig.enabled || !sensorState.coldNtcOk) {
    controlState.effectiveTargetC = controlConfig.targetC;
    controlState.dewClampActive = false;
    setPwmDuty(0.0f);
    resetPid();
    return;
  }

  if (controlConfig.hotSensorEnabled && sensorState.hotNtcOk && sensorState.hotC >= controlConfig.maxHotSideC) {
    controlState.hotProtectionActive = true;
    controlState.effectiveTargetC = controlConfig.targetC;
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
  sensor["ambient_c"] = sensorState.ambientC;
  sensor["humidity_pct"] = sensorState.humidityPct;
  sensor["dew_point_c"] = sensorState.dewPointC;
  sensor["current_a"] = sensorState.currentA;
  sensor["bus_voltage_v"] = sensorState.busVoltageV;
  sensor["cold_ntc_ok"] = sensorState.coldNtcOk;
  sensor["hot_ntc_ok"] = sensorState.hotNtcOk;
  sensor["bme280_ok"] = sensorState.bmeOk;
  sensor["ina219_ok"] = sensorState.inaOk;

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
  board["free_heap_bytes"] = ESP.getFreeHeap();
  board["wifi_rssi_dbm"] = WiFi.isConnected() ? WiFi.RSSI() : 0;
  board["wifi_bssid"] = WiFi.isConnected() ? WiFi.BSSIDstr() : String();
  board["ble_connected"] = bleServer != nullptr ? bleServer->getConnectedCount() > 0 : false;
  board["ble_clients"] = bleServer != nullptr ? bleServer->getConnectedCount() : 0;
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

  if (doc["enabled"].is<bool>()) {
    controlConfig.enabled = doc["enabled"].as<bool>();
    changed = true;
  }
  if (doc["hot_sensor_enabled"].is<bool>()) {
    controlConfig.hotSensorEnabled = doc["hot_sensor_enabled"].as<bool>();
    changed = true;
  }
  if (doc["target_c"].is<float>() || doc["target_c"].is<int>()) {
    controlConfig.targetC = doc["target_c"].as<float>();
    changed = true;
  }
  if (doc["dew_margin_c"].is<float>() || doc["dew_margin_c"].is<int>()) {
    controlConfig.dewMarginC = clampf(doc["dew_margin_c"].as<float>(), 0.0f, 20.0f);
    changed = true;
  }
  if (doc["max_duty"].is<float>() || doc["max_duty"].is<int>()) {
    controlConfig.maxDuty = clampf(doc["max_duty"].as<float>(), 0.0f, 1.0f);
    changed = true;
  }
  if (doc["max_hot_side_c"].is<float>() || doc["max_hot_side_c"].is<int>()) {
    controlConfig.maxHotSideC = clampf(doc["max_hot_side_c"].as<float>(), 20.0f, 100.0f);
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
    if (pid["kp"].is<float>() || pid["kp"].is<int>()) {
      pidConfig.kp = pid["kp"].as<float>();
      changed = true;
    }
    if (pid["ki"].is<float>() || pid["ki"].is<int>()) {
      pidConfig.ki = pid["ki"].as<float>();
      changed = true;
    }
    if (pid["kd"].is<float>() || pid["kd"].is<int>()) {
      pidConfig.kd = pid["kd"].as<float>();
      changed = true;
    }
    if (changed) {
      controlState.pidProfile = "manual";
    }
  }

  if (doc["calibration"].is<JsonObject>()) {
    const JsonObjectConst cal = doc["calibration"].as<JsonObjectConst>();
    if (cal["cold_offset_c"].is<float>() || cal["cold_offset_c"].is<int>()) {
      calibration.coldOffsetC = cal["cold_offset_c"].as<float>();
      changed = true;
    }
    if (cal["hot_offset_c"].is<float>() || cal["hot_offset_c"].is<int>()) {
      calibration.hotOffsetC = cal["hot_offset_c"].as<float>();
      changed = true;
    }
    if (cal["ambient_offset_c"].is<float>() || cal["ambient_offset_c"].is<int>()) {
      calibration.ambientTempOffsetC = cal["ambient_offset_c"].as<float>();
      changed = true;
    }
    if (cal["humidity_offset_pct"].is<float>() || cal["humidity_offset_pct"].is<int>()) {
      calibration.humidityOffsetPct = cal["humidity_offset_pct"].as<float>();
      changed = true;
    }
    if (cal["current_offset_a"].is<float>() || cal["current_offset_a"].is<int>()) {
      calibration.currentOffsetA = cal["current_offset_a"].as<float>();
      changed = true;
    }
    if (cal["current_scale"].is<float>() || cal["current_scale"].is<int>()) {
      calibration.currentScale = cal["current_scale"].as<float>();
      changed = true;
    }
  }

  if (!changed) {
    message = "Nessun parametro valido ricevuto";
    return false;
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
    server.send(400, "application/json", String("{\"error\":\"") + message + "\"}");
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
    server.send(200, "text/html", buildPortalHtml());
    return;
  }

  String html;
  html.reserve(15000);
  html += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>ASI585MC Cooler</title><style>body{margin:0;font-family:Arial,sans-serif;background:#08111f;color:#e8f0ff}"
            ".wrap{max-width:1100px;margin:auto;padding:20px}.hero{display:flex;justify-content:space-between;gap:16px;align-items:center;margin-bottom:18px}"
            ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:16px}.card{background:#111d32;border:1px solid #243757;border-radius:16px;padding:16px;box-shadow:0 10px 30px rgba(0,0,0,.25);display:flex;flex-direction:column}.wide{grid-column:1/-1}"
            "h1,h2{margin:0 0 12px}label{display:block;font-size:13px;color:#9fb5d8;margin:10px 0 6px}input,select{width:100%;box-sizing:border-box;background:#091221;color:#e8f0ff;border:1px solid #30496e;border-radius:10px;padding:10px}"
            ".uf{position:relative}.uf input{text-align:right;padding-right:48px}.uf span{position:absolute;right:12px;top:50%;transform:translateY(-50%);font-size:13px;color:#9fb5d8;pointer-events:none}"
            ".actions{display:flex;justify-content:space-between;align-items:flex-end;gap:12px;margin-top:auto;padding-top:12px}.actions .right{text-align:right}"
            "button{background:#3385ff;color:white;border:none;border-radius:10px;padding:10px 14px;cursor:pointer;margin:6px 6px 0 0}button.alt{background:#223754}button.warn{background:#b84f32}"
            ".metric{font-size:28px;font-weight:bold}.meta{color:#9fb5d8;font-size:13px}.mini{font-size:12px;line-height:1.5;color:#b7c8e6}.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.diag{display:grid;grid-template-columns:1fr 1fr;gap:6px 12px}.diag div:nth-child(odd){color:#9fb5d8}.diag div:nth-child(even){text-align:right}.chartWrap{background:#091221;border:1px solid #243757;border-radius:12px;padding:10px}.chartLegend{display:flex;flex-wrap:wrap;gap:12px;margin:0 0 8px;font-size:12px;color:#b7c8e6}.legendItem{display:flex;align-items:center;gap:6px}.sw{width:10px;height:10px;border-radius:50%}.chartWrap canvas{width:100%;height:220px;display:block}"
            "@media(max-width:640px){.row{grid-template-columns:1fr}.hero{flex-direction:column;align-items:flex-start}}</style></head><body>");
  html += F("<div class='wrap'><div class='hero'><div><h1>ZWO ASI Smart Cooler</h1><div class='meta'>Firmware <span id='fwVersion'>--</span> | <a id='projectLink' href='#' target='_blank' style='color:#8fc7ff'>https://github.com/naamah75/asi-smart-cooler-diy</a></div></div><div><button onclick='refreshStatus()'>Aggiorna</button><button class='alt' onclick='applyBaseTune()'>Base tune PID</button></div></div>");
  html += F("<div class='grid'><div class='card'><h2>Stato</h2><div class='row'><div><div class='metric' id='coldMetric'>--</div><div class='meta'>Temperatura fredda</div></div><div><div class='metric' id='dutyMetric'>--</div><div class='meta'>Duty PWM</div></div></div><div class='row'><div><div class='metric' id='dewMetric'>--</div><div class='meta'>Dew point</div></div><div><div class='metric' id='currentMetric'>--</div><div class='meta'>Corrente Peltier</div></div></div><div class='row'><div><div class='metric' id='hotMetric'>--</div><div class='meta'>Lato caldo</div></div><div><div class='metric' id='ambientMetric'>--</div><div class='meta'>Ambiente</div></div></div></div>");
  html += F("<div class='card wide'><h2>Grafici</h2><div class='chartWrap'><div class='chartLegend'><div class='legendItem'><span class='sw' style='background:#55d6ff'></span><span>Cold side</span></div><div class='legendItem'><span class='sw' style='background:#ff8f6b'></span><span>Hot side</span></div><div class='legendItem'><span class='sw' style='background:#6ee7b7'></span><span>Ambient</span></div><div class='legendItem'><span class='sw' style='background:#3a86ff'></span><span>Peltier power</span></div></div><canvas id='trendChart' width='1000' height='220'></canvas></div></div>");
  html += F("<div class='card'><h2>Controllo</h2><div class='row'><div><label>Target freddo</label><div class='uf'><input id='target_c' type='number' step='0.1'><span>°C</span></div></div><div><label>Margine dew point</label><div class='uf'><input id='dew_margin_c' type='number' step='0.1'><span>°C</span></div></div></div><div class='row'><div><label>Duty max</label><div class='uf'><input id='max_duty' type='number' step='0.01' min='0' max='1'><span>x</span></div></div><div><label>Temperatura max lato caldo</label><div class='uf'><input id='max_hot_side_c' type='number' step='0.1'><span>°C</span></div></div></div><div class='row'><div><label>Controllo</label><select id='enabled'><option value='true'>Abilitato</option><option value='false'>Disabilitato</option></select></div><div><label>Sensore caldo</label><select id='hot_sensor_enabled'><option value='true'>Abilitato</option><option value='false'>Disabilitato</option></select></div></div><div class='actions'><div><button onclick='saveConfig()'>Salva controllo</button></div><div class='right'><button class='warn' onclick='disableCooler()'>Stop</button></div></div></div>");
  html += F("<div class='card'><h2>PID</h2><div class='row'><div><label>Kp</label><input id='kp' type='number' step='0.01'></div><div><label>Ki</label><input id='ki' type='number' step='0.01'></div></div><div class='row'><div><label>Kd</label><input id='kd' type='number' step='0.01'></div><div><label>Profilo rapido</label><select id='pid_profile'><option value='manual'>Manuale</option><option value='soft'>Soft</option><option value='normal'>Normal</option><option value='aggressive'>Aggressive</option></select></div></div><div class='actions'><div><button onclick='savePid()'>Salva PID</button></div><div class='right'><button class='alt' onclick='savePidProfile()'>Applica profilo</button></div></div></div>");
  html += F("<div class='card'><h2>Calibrazioni</h2><div class='row'><div><label>Offset freddo</label><div class='uf'><input id='cold_offset_c' type='number' step='0.1'><span>°C</span></div></div><div><label>Offset caldo</label><div class='uf'><input id='hot_offset_c' type='number' step='0.1'><span>°C</span></div></div></div><div class='row'><div><label>Offset ambiente</label><div class='uf'><input id='ambient_offset_c' type='number' step='0.1'><span>°C</span></div></div><div><label>Offset umidita</label><div class='uf'><input id='humidity_offset_pct' type='number' step='0.1'><span>%</span></div></div></div><div class='row'><div><label>Offset corrente</label><div class='uf'><input id='current_offset_a' type='number' step='0.01'><span>A</span></div></div><div><label>Scala corrente</label><div class='uf'><input id='current_scale' type='number' step='0.01'><span>x</span></div></div></div><div class='actions'><div><button onclick='saveCalibration()'>Salva calibrazioni</button></div><div></div></div></div>");
  html += F("<div class='card'><h2>Preimpostazioni</h2><label>Preset</label><select id='preset_name'><option value='estate'>Estate</option><option value='inverno'>Inverno</option><option value='deep_cooling'>Deep cooling</option></select><div class='meta' id='presetInfo'>Snapshot di target, dew margin, duty max e protezione lato caldo.</div><div class='actions'><div><button onclick='applyPreset()'>Applica preset</button></div><div class='right'><button class='alt' onclick='savePreset()'>Salva preset corrente</button></div></div></div>");
  html += F("<div class='card'><h2>Statistiche</h2><div class='mini diag'><div>WiFi signal</div><div id='wifiRssi'>--</div><div>BSSID</div><div id='wifiBssid'>--</div><div>IP</div><div id='boardIp'>--</div><div>BLE</div><div id='bleState'>--</div><div>Free RAM</div><div id='freeHeap'>--</div><div>CPU temp</div><div id='cpuTemp'>--</div><div>Avvio</div><div id='bootTime'>--</div></div></div>");
  html += F("<script>function fmt(v,u=''){return Number.isFinite(v)?v.toFixed(2)+u:'--'}function boolVal(id){return document.getElementById(id).value==='true'}function numVal(id){return parseFloat(document.getElementById(id).value)}"
            "const hist={cold:[],hot:[],ambient:[],power:[]};function pushHist(k,v){if(!Number.isFinite(v))return;hist[k].push(v);if(hist[k].length>90)hist[k].shift()}function fillField(id,v){if(Number.isFinite(v))document.getElementById(id).value=v}function fillBool(id,v){document.getElementById(id).value=v?'true':'false'}"
            "async function api(path,body){const r=await fetch(path,{method:body?'POST':'GET',headers:{'Content-Type':'application/json'},body:body?JSON.stringify(body):undefined});return await r.json()}"
            "function drawLine(ctx,data,minV,maxV,color,left,right,top,bottom,w){if(data.length<2)return;ctx.beginPath();ctx.lineWidth=2;ctx.strokeStyle=color;data.forEach((v,i)=>{const px=left+(i/Math.max(data.length-1,1))*w;const py=bottom-((v-minV)/Math.max(maxV-minV,0.001))*(bottom-top);if(i===0)ctx.moveTo(px,py);else ctx.lineTo(px,py)});ctx.stroke()}function redrawCharts(){const c=document.getElementById('trendChart');const x=c.getContext('2d');const left=42,right=42,top=12,bottom=c.height-18,w=c.width-left-right;x.clearRect(0,0,c.width,c.height);const temps=[...hist.cold,...hist.hot,...hist.ambient].filter(Number.isFinite);const tMin=temps.length?Math.min(...temps)-1:0;const tMax=temps.length?Math.max(...temps)+1:10;for(let i=0;i<5;i++){const y=top+i*((bottom-top)/4);x.strokeStyle='#223754';x.lineWidth=1;x.beginPath();x.moveTo(left,y);x.lineTo(c.width-right,y);x.stroke();const tVal=(tMax-((tMax-tMin)/4)*i).toFixed(0);x.fillStyle='#9fb5d8';x.font='11px Arial';x.fillText(tVal,4,y+4);const pVal=(100-25*i).toFixed(0);x.fillText(pVal,c.width-right+8,y+4)}x.strokeStyle='#3b4f73';x.beginPath();x.moveTo(left,top);x.lineTo(left,bottom);x.lineTo(c.width-right,bottom);x.lineTo(c.width-right,top);x.stroke();drawLine(x,hist.cold,tMin,tMax,'#55d6ff',left,right,top,bottom,w);drawLine(x,hist.hot,tMin,tMax,'#ff8f6b',left,right,top,bottom,w);drawLine(x,hist.ambient,tMin,tMax,'#6ee7b7',left,right,top,bottom,w);drawLine(x,hist.power,0,100,'#3a86ff',left,right,top,bottom,w)}function syncUi(j){coldMetric.textContent=fmt(j.sensor.cold_c,' °C');dewMetric.textContent=fmt(j.sensor.dew_point_c,' °C');currentMetric.textContent=fmt(j.sensor.current_a,' A');hotMetric.textContent=fmt(j.sensor.hot_c,' °C');ambientMetric.textContent=fmt(j.sensor.ambient_c,' °C');dutyMetric.textContent=fmt(j.control.pwm_duty*100,' %');fwVersion.textContent=j.board.firmware_version||'--';projectLink.href=j.board.project_url||'#';projectLink.textContent=j.board.project_url||'--';boardIp.textContent=j.network.ip||'--';wifiRssi.textContent=Number.isFinite(j.board.wifi_rssi_dbm)?j.board.wifi_rssi_dbm+' dBm':'--';wifiBssid.textContent=j.board.wifi_bssid||'--';bleState.textContent=j.board.ble_connected?('Connected '+j.board.ble_clients):'Idle';freeHeap.textContent=Number.isFinite(j.board.free_heap_bytes)?j.board.free_heap_bytes+' B':'--';cpuTemp.textContent=fmt(j.board.cpu_temp_c,' °C');bootTime.textContent=j.board.boot_time_local||'--';pushHist('cold',j.sensor.cold_c);pushHist('hot',j.sensor.hot_c);pushHist('ambient',j.sensor.ambient_c);pushHist('power',j.control.pwm_duty*100);redrawCharts();"
            "fillField('target_c',j.control.target_c);fillField('dew_margin_c',j.control.dew_margin_c);fillField('max_duty',j.control.max_duty);fillField('max_hot_side_c',j.control.max_hot_side_c);fillBool('enabled',j.control.enabled);fillBool('hot_sensor_enabled',j.control.hot_sensor_enabled);"
            "fillField('kp',j.pid.kp);fillField('ki',j.pid.ki);fillField('kd',j.pid.kd);document.getElementById('pid_profile').value=['soft','normal','aggressive'].includes(j.pid.profile)?j.pid.profile:'manual';"
            "fillField('cold_offset_c',j.calibration.cold_offset_c);fillField('hot_offset_c',j.calibration.hot_offset_c);fillField('ambient_offset_c',j.calibration.ambient_offset_c);fillField('humidity_offset_pct',j.calibration.humidity_offset_pct);fillField('current_offset_a',j.calibration.current_offset_a);fillField('current_scale',j.calibration.current_scale);const p=document.getElementById('preset_name').value;const pr=j.presets&&j.presets[p]?j.presets[p]:null;document.getElementById('presetInfo').textContent=pr?('target '+fmt(pr.target_c,' °C')+', dew '+fmt(pr.dew_margin_c,' °C')+', duty '+fmt(pr.max_duty*100,' %')):'Preset non disponibile'}"
            "async function refreshStatus(){syncUi(await api('/api/status'))}"
            "async function saveConfig(){syncUi(await api('/api/config',{enabled:boolVal('enabled'),hot_sensor_enabled:boolVal('hot_sensor_enabled'),target_c:numVal('target_c'),dew_margin_c:numVal('dew_margin_c'),max_duty:numVal('max_duty'),max_hot_side_c:numVal('max_hot_side_c')}))}"
            "async function disableCooler(){syncUi(await api('/api/config',{enabled:false}))}"
            "async function savePid(){syncUi(await api('/api/config',{pid:{kp:numVal('kp'),ki:numVal('ki'),kd:numVal('kd')}}))}"
            "async function savePidProfile(){syncUi(await api('/api/config',{pid_profile:document.getElementById('pid_profile').value}))}"
            "async function applyBaseTune(){syncUi(await api('/api/pid/base-tune',{}))}"
            "async function saveCalibration(){syncUi(await api('/api/config',{calibration:{cold_offset_c:numVal('cold_offset_c'),hot_offset_c:numVal('hot_offset_c'),ambient_offset_c:numVal('ambient_offset_c'),humidity_offset_pct:numVal('humidity_offset_pct'),current_offset_a:numVal('current_offset_a'),current_scale:numVal('current_scale')}}))}"
            "async function applyPreset(){syncUi(await api('/api/preset/apply',{name:document.getElementById('preset_name').value}))}"
            "async function savePreset(){syncUi(await api('/api/preset/save',{name:document.getElementById('preset_name').value}))}document.getElementById('preset_name').addEventListener('change',refreshStatus);"
            "refreshStatus();setInterval(refreshStatus,2000);</script></div></body></html>");
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

void setupSensors() {
  Wire.begin(pins::I2C_SDA, pins::I2C_SCL);
  sensorState.bmeOk = bme.begin(0x76) || bme.begin(0x77);
  sensorState.inaOk = ina219.begin();
  if (sensorState.inaOk) {
    ina219.setCalibration_32V_2A();
  }

  analogReadResolution(12);
  analogSetPinAttenuation(pins::COLD_NTC_ADC, ADC_11db);
  analogSetPinAttenuation(pins::HOT_NTC_ADC, ADC_11db);
}

void setupControl() {
  ledcSetup(pwm::CHANNEL, pwm::FREQUENCY_HZ, pwm::RESOLUTION_BITS);
  ledcAttachPin(pins::PELTIER_PWM, pwm::CHANNEL);
  setPwmDuty(0.0f);
}

void setup() {
  Serial.begin(115200);
  delay(300);
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

  if (bleStatusCharacteristic != nullptr && now - lastBleNotifyMs >= timing::STATUS_NOTIFY_MS) {
    lastBleNotifyMs = now;
    syncBleCharacteristics(true);
  }

  if (now - lastSerialLogMs >= timing::SERIAL_LOG_MS) {
    lastSerialLogMs = now;
    logStatusToSerial();
  }
}
