
#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// ===================================
// EcoDorm - ESP32 #1 + DHT22
// ===================================

// 1. Wi-Fi (2.4 GHz)
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// 2. EcoDorm API Key from Node-RED
const char* API_KEY = "YOUR_ECODORM_API_KEY";

// 3. Proxmox HTTPS Endpoint
const char* SERVER_URL =
  "https://YOUR_HOST/api/ecodorm/telemetry";

// 4. DHT22
#define DHTPIN 27
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

// 5. Thresholds (Demo)
const float TEMP_LIMIT = 32.0;
const float HUMIDITY_LIMIT = 75.0;

// 6. Intervals
const unsigned long READ_INTERVAL = 5000;   // 5 วินาที
const unsigned long SEND_INTERVAL = 60000;  // 1 นาที

unsigned long lastRead = 0;
unsigned long lastSend = 0;
unsigned long lastWiFiRetry = 0;

bool firstSend = true;

// ===================================
// HTTPS POST
// ===================================

void sendToServer(float temperature, float humidity) {

  WiFiClientSecure client;

  // Verify HTTPS Certificate จริง
  // ESP32 Core 3.3.12 ขึ้นไป
  client.useBuiltinCACertBundle();

  client.setHandshakeTimeout(15);

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);

  if (!http.begin(client, SERVER_URL)) {
    Serial.println("HTTPS begin failed");
    return;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", API_KEY);

  // JSON match with Node-RED
  String json = "{\"device_id\":\"temp01\",";
  json += "\"temperature\":";
  json += String(temperature, 2);
  json += ",\"humidity\":";
  json += String(humidity, 2);
  json += "}";

  Serial.println("Sending HTTPS...");
  Serial.println(json);

  int httpCode = http.POST(json);

  Serial.print("HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode == 202) {
    Serial.println("SUCCESS: Node-RED accepted data");
  } else if (httpCode > 0) {
    Serial.println("Server rejected request");
    Serial.println(http.getString());
  } else {
    Serial.print("HTTPS error: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// ===================================
// SETUP
// ===================================

void setup() {

  Serial.begin(115200);
  dht.begin();

  Serial.println();
  Serial.println("EcoDorm - ESP32 #1");
  Serial.println("DHT22 + WiFi + HTTPS");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println("Connecting to WiFi...");

  // HTTPS Certificate ต้องใช้เวลาที่ถูกต้อง
  configTime(0, 0, "pool.ntp.org", "time.google.com");
}

// ===================================
// LOOP
// ===================================

void loop() {

  unsigned long now = millis();

  // ลองเชื่อม Wi-Fi ใหม่เมื่อหลุด
  if (WiFi.status() != WL_CONNECTED &&
      now - lastWiFiRetry >= 10000) {

    lastWiFiRetry = now;
    WiFi.reconnect();
  }

  // อ่าน Sensor ทุก 5 วินาที
  if (now - lastRead < READ_INTERVAL) {
    return;
  }

  lastRead = now;

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  // STEP 1: Data Validation
  if (isnan(temperature) || isnan(humidity) ||
      temperature < 0 || temperature > 60 ||
      humidity < 0 || humidity > 100) {

    Serial.println("INVALID DATA: DHT22 rejected");
    return;
  }

  // STEP 2: Edge Threshold Detection
  bool tempWarning = temperature > TEMP_LIMIT;
  bool humidityWarning = humidity > HUMIDITY_LIMIT;

  Serial.println("-------------------------");

  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" C");

  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  if (tempWarning || humidityWarning) {

    Serial.println("STATUS: WARNING");

    if (tempWarning) {
      Serial.println("ALERT: High Temperature!");
    }

    if (humidityWarning) {
      Serial.println("ALERT: High Humidity!");
    }

  } else {
    Serial.println("STATUS: NORMAL");
  }

  // STEP 3: Check Wi-Fi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected");
    return;
  }

  // STEP 4: Check Time Sync for TLS
  if (time(nullptr) < 1700000000) {
    Serial.println("Waiting for NTP time sync...");
    return;
  }

  // STEP 5: Send HTTPS every 1 minute
  if (firstSend || now - lastSend >= SEND_INTERVAL) {

    firstSend = false;
    lastSend = millis();

    sendToServer(temperature, humidity);
  }
}
