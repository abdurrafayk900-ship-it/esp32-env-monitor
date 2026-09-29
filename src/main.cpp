/*
 * ESP32 IoT Environmental Monitor
 * --------------------------------
 * Reads temperature, humidity and pressure (BME280), light (BH1750) and
 * air quality (MQ-135), then:
 *   - publishes JSON over MQTT with Home Assistant auto-discovery
 *   - serves a live dashboard + JSON API on the local network
 *   - drives status/alert LEDs and a buzzer when thresholds are crossed
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <BH1750.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

#include "config.h"
#include "secrets.h"

Adafruit_BME280 bme;
BH1750 lightMeter(BH1750_ADDR);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
WebServer server(80);

struct Reading {
  float temperature = NAN;
  float humidity = NAN;
  float pressure = NAN;
  float lux = NAN;
  int airQuality = -1;
  bool alert = false;
  String alertReason;
};

Reading latest;
bool bmeOk = false;
bool lightOk = false;
unsigned long lastPublish = 0;
unsigned long lastMqttAttempt = 0;

// ------------------------------------------------------------------ WiFi
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_ID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to %s", WIFI_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWi-Fi OK, IP %s, RSSI %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    configTime(0, 0, "pool.ntp.org", "time.google.com");
  } else {
    Serial.println("\nWi-Fi failed, restarting in 10 s");
    delay(10000);
    ESP.restart();
  }
}

// ------------------------------------------------------------------ Sensors
Reading sampleSensors() {
  Reading r;
  float t = 0, h = 0, p = 0, l = 0;
  long aq = 0;

  for (int i = 0; i < SAMPLE_COUNT; i++) {
    if (bmeOk) {
      t += bme.readTemperature();
      h += bme.readHumidity();
      p += bme.readPressure() / 100.0f;  // Pa -> hPa
    }
    if (lightOk) l += lightMeter.readLightLevel();
    aq += analogRead(PIN_MQ135);
    delay(100);
  }

  if (bmeOk) {
    r.temperature = t / SAMPLE_COUNT;
    r.humidity = h / SAMPLE_COUNT;
    r.pressure = p / SAMPLE_COUNT;
  }
  if (lightOk) r.lux = l / SAMPLE_COUNT;
  r.airQuality = aq / SAMPLE_COUNT;

  // Threshold checks
  if (!isnan(r.temperature) && r.temperature > TEMP_HIGH_C) r.alertReason += "temp_high ";
  if (!isnan(r.temperature) && r.temperature < TEMP_LOW_C)  r.alertReason += "temp_low ";
  if (!isnan(r.humidity) && r.humidity > HUMIDITY_HIGH)     r.alertReason += "humidity_high ";
  if (r.airQuality > AIR_QUALITY_POOR)                      r.alertReason += "air_quality_poor ";
  r.alertReason.trim();
  r.alert = r.alertReason.length() > 0;
  return r;
}

void updateIndicators(const Reading& r) {
  digitalWrite(PIN_LED_OK, r.alert ? LOW : HIGH);
  digitalWrite(PIN_LED_ALERT, r.alert ? HIGH : LOW);
  if (r.alert) {
    tone(PIN_BUZZER, 2000, 150);
  }
}

String toJson(const Reading& r) {
  JsonDocument doc;
  doc["device"] = DEVICE_ID;
  doc["timestamp"] = (long)time(nullptr);
  if (!isnan(r.temperature)) doc["temperature"] = serialized(String(r.temperature, 2));
  if (!isnan(r.humidity))    doc["humidity"] = serialized(String(r.humidity, 1));
  if (!isnan(r.pressure))    doc["pressure"] = serialized(String(r.pressure, 1));
  if (!isnan(r.lux))         doc["lux"] = serialized(String(r.lux, 0));
  doc["air_quality"] = r.airQuality;
  doc["alert"] = r.alert;
  doc["alert_reason"] = r.alertReason;
  doc["rssi"] = WiFi.RSSI();
  doc["uptime_s"] = millis() / 1000;
  String out;
  serializeJson(doc, out);
  return out;
}

// ------------------------------------------------------------------ MQTT
void publishDiscovery(const char* key, const char* name, const char* unit,
                      const char* deviceClass) {
  JsonDocument doc;
  doc["name"] = name;
  doc["unique_id"] = String(DEVICE_ID) + "_" + key;
  doc["state_topic"] = TOPIC_STATE;
  doc["availability_topic"] = TOPIC_STATUS;
  doc["value_template"] = String("{{ value_json.") + key + " }}";
  if (unit) doc["unit_of_measurement"] = unit;
  if (deviceClass) doc["device_class"] = deviceClass;
  JsonObject dev = doc["device"].to<JsonObject>();
  dev["identifiers"][0] = DEVICE_ID;
  dev["name"] = DEVICE_NAME;
  dev["model"] = "ESP32 + BME280 + BH1750 + MQ-135";

  String topic = String(HA_DISCOVERY_PREFIX) + "/sensor/" + DEVICE_ID + "/" + key + "/config";
  String payload;
  serializeJson(doc, payload);
  mqtt.publish(topic.c_str(), payload.c_str(), true);
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.printf("MQTT cmd: %s\n", msg.c_str());
  if (msg == "publish") {
    lastPublish = 0;  // force an immediate reading
  } else if (msg == "restart") {
    ESP.restart();
  }
}

void ensureMqtt() {
  if (mqtt.connected() || millis() - lastMqttAttempt < 5000) return;
  lastMqttAttempt = millis();
  Serial.print("Connecting to MQTT... ");
  // Last Will: broker marks us offline if we drop off unexpectedly
  if (mqtt.connect(DEVICE_ID, MQTT_USER, MQTT_PASSWORD, TOPIC_STATUS, 1, true, "offline")) {
    Serial.println("OK");
    mqtt.publish(TOPIC_STATUS, "online", true);
    mqtt.subscribe(TOPIC_COMMAND);
    publishDiscovery("temperature", "Temperature", "°C", "temperature");
    publishDiscovery("humidity", "Humidity", "%", "humidity");
    publishDiscovery("pressure", "Pressure", "hPa", "atmospheric_pressure");
    publishDiscovery("lux", "Illuminance", "lx", "illuminance");
    publishDiscovery("air_quality", "Air Quality (raw)", nullptr, nullptr);
  } else {
    Serial.printf("failed, rc=%d\n", mqtt.state());
  }
}

// ------------------------------------------------------------------ Web dashboard
const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Env Monitor</title>
<style>
body{font-family:system-ui;background:#0f172a;color:#e2e8f0;margin:0;padding:24px}
h1{font-size:20px;margin:0 0 16px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}
.card{background:#1e293b;border-radius:12px;padding:16px}
.v{font-size:28px;font-weight:600}.l{color:#94a3b8;font-size:13px}
.alert{background:#7f1d1d}
</style></head><body>
<h1>Environmental Monitor</h1><div class="grid" id="g"></div>
<p class="l" id="s"></p>
<script>
const F=[["temperature","Temperature","°C"],["humidity","Humidity","%"],["pressure","Pressure","hPa"],
["lux","Light","lx"],["air_quality","Air quality (raw)",""]];
async function tick(){
 const d=await (await fetch('/api/state')).json();
 g.innerHTML=F.map(([k,l,u])=>`<div class="card ${d.alert?'alert':''}"><div class="l">${l}</div>
 <div class="v">${d[k]??'–'} ${u}</div></div>`).join('');
 s.textContent=(d.alert?'ALERT: '+d.alert_reason+' · ':'All normal · ')+'RSSI '+d.rssi+' dBm · up '+d.uptime_s+' s';
}
tick();setInterval(tick,5000);
</script></body></html>
)HTML";

void setupWeb() {
  server.on("/", []() { server.send_P(200, "text/html", DASHBOARD_HTML); });
  server.on("/api/state", []() { server.send(200, "application/json", toJson(latest)); });
  server.on("/api/health", []() {
    JsonDocument doc;
    doc["bme280"] = bmeOk;
    doc["bh1750"] = lightOk;
    doc["mqtt"] = mqtt.connected();
    doc["free_heap"] = ESP.getFreeHeap();
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });
  server.begin();
}

// ------------------------------------------------------------------ Arduino
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_OK, OUTPUT);
  pinMode(PIN_LED_ALERT, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  analogReadResolution(12);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  bmeOk = bme.begin(BME280_ADDR);
  lightOk = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  Serial.printf("BME280: %s, BH1750: %s\n", bmeOk ? "OK" : "NOT FOUND", lightOk ? "OK" : "NOT FOUND");

  connectWiFi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(MQTT_MAX_PACKET_SIZE);
  setupWeb();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  ensureMqtt();
  mqtt.loop();
  server.handleClient();

  if (lastPublish == 0 || millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = millis();
    latest = sampleSensors();
    updateIndicators(latest);
    String payload = toJson(latest);
    Serial.println(payload);
    if (mqtt.connected()) mqtt.publish(TOPIC_STATE, payload.c_str(), true);
  }
}
