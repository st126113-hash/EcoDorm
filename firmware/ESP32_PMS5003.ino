
#include <Arduino.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <Adafruit_PM25AQI.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// =======================================
// EcoDorm - ESP32 #2
// PMS5003 + LCD 20x4 + HTTPS
// =======================================

// STEP 1: Wi-Fi (2.4 GHz)
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// STEP 2: EcoDorm API Key
// Key 

const char* API_KEY = "YOUR_ECODORM_API_KEY";

// STEP 3: HTTPS Endpoint
const char* SERVER_URL =
  "https://YOUR_HOST/api/ecodorm/telemetry";

// =======================================
// Sensor and LCD
// =======================================

HardwareSerial pmsSerial(2);
Adafruit_PM25AQI pms;

hd44780_I2Cexp lcd(0x27);

// Prototype thresholds
const int PM25_WARNING_LIMIT = 35;
const int PM25_CRITICAL_LIMIT = 75;
const int PM_MAX_VALID = 1000;

// Sensor reads approximately every 2 seconds
const unsigned long READ_INTERVAL = 2000;

// Send telemetry every 1 minute
const unsigned long SEND_INTERVAL = 60000;

unsigned long lastValidRead = 0;
unsigned long lastSend = 0;
unsigned long lastWiFiRetry = 0;

bool firstSend = true;

// =======================================
// LCD: Display complete 20-character row
// =======================================

void showLine(int row, const char* message) {
  char padded[21];

  snprintf(padded, sizeof(padded),
           "%-20.20s", message);

  lcd.setCursor(0, row);
  lcd.print(padded);
}

// =======================================
// HTTPS POST to Proxmox
// =======================================

void sendToServer(int pm1,
                  int pm25,
                  int pm10,
                  const char* statusText) {

  WiFiClientSecure client;

  // Verify HTTPS certificate
  // ESP32 Core 3.3.12
  client.useBuiltinCACertBundle();
  client.setHandshakeTimeout(15);

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);

  if (!http.begin(client, SERVER_URL)) {
    Serial.println("HTTPS begin failed");
    return;
  }

  http.addHeader(
    "Content-Type", "application/json"
  );

  http.addHeader("x-api-key", API_KEY);

  // JSON format compatible with Node-RED
  String json = "{\"device_id\":\"air01\",";
  json += "\"pm1\":";
  json += String(pm1);
  json += ",\"pm25\":";
  json += String(pm25);
  json += ",\"pm10\":";
  json += String(pm10);
  json += ",\"status\":\"";
  json += statusText;
  json += "\"}";

  Serial.println("Sending HTTPS...");
  Serial.println(json);

  int httpCode = http.POST(json);

  Serial.print("HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode == 202) {
    Serial.println(
      "SUCCESS: Node-RED accepted PMS5003 data"
    );
  } else if (httpCode > 0) {
    Serial.println("Server rejected request");
    Serial.println(http.getString());
  } else {
    Serial.print("HTTPS error: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// =======================================
// SETUP
// =======================================

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("EcoDorm - ESP32 #2");
  Serial.println("PMS5003 + LCD + WiFi + HTTPS");

  // STEP 1: Initialize LCD
  // Keep the working I2C pin configuration
  Wire.setPins(25, 26);
  Wire.begin();
  Wire.setClock(100000);

  int lcdStatus = lcd.begin(20, 4);

  Serial.print("LCD begin status: ");
  Serial.println(lcdStatus);

  if (lcdStatus != 0) {
    Serial.println("LCD initialization FAILED");

    while (true) {
      delay(1000);
    }
  }

  lcd.backlight();
  lcd.clear();

  showLine(0, "EcoDorm Monitoring");
  showLine(1, "PM2.5 Sensor");
  showLine(2, "Initializing...");
  showLine(3, "Please wait");

  Serial.println("LCD initialized!");

  // STEP 2: Initialize PMS5003
  // PMS5003 TXD -> ESP32 GPIO16
  pmsSerial.begin(
    9600, SERIAL_8N1, 16, -1
  );

  delay(3000);

  if (!pms.begin_UART(&pmsSerial)) {

    Serial.println("PMS5003 setup FAILED");

    showLine(1, "Sensor ERROR");
    showLine(2, "Check PMS5003");
    showLine(3, "Check wiring");

    while (true) {
      delay(1000);
    }
  }

  Serial.println("PMS5003 initialized!");

  // STEP 3: Start Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println("Connecting to WiFi...");

  // Required for HTTPS certificate validation
  configTime(
    0, 0,
    "pool.ntp.org",
    "time.google.com"
  );

  // STEP 4: PMS5003 warm-up
  showLine(0, "EcoDorm Monitoring");
  showLine(1, "PMS5003 warming up");
  showLine(2, "Please wait 30 sec");
  showLine(3, "Status: STARTING");

  delay(30000);

  lastValidRead = millis();

  showLine(1, "Waiting for data");
  showLine(2, "Status: WAITING");
  showLine(3, "Sensor: PMS5003");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi not connected yet");
  }
}

// =======================================
// LOOP
// =======================================

void loop() {

  unsigned long now = millis();

  // STEP 1: Maintain Wi-Fi connection
  if (WiFi.status() != WL_CONNECTED &&
      now - lastWiFiRetry >= 10000) {

    lastWiFiRetry = now;
    WiFi.reconnect();
  }

  PM25_AQI_Data data;

  // STEP 2: Read PMS5003
  if (!pms.read(&data)) {

    Serial.println("Waiting for PMS5003 data...");

    if (millis() - lastValidRead > 10000) {
      showLine(1, "PM2.5: -- ug/m3");
      showLine(2, "Status: NO DATA");
    }

    delay(READ_INTERVAL);
    return;
  }

  int pm1  = data.pm10_env;
  int pm25 = data.pm25_env;
  int pm10 = data.pm100_env;

  // STEP 3: Data Validation
  if (pm1 > PM_MAX_VALID ||
      pm25 > PM_MAX_VALID ||
      pm10 > PM_MAX_VALID ||
      pm1 > pm25 ||
      pm25 > pm10) {

    Serial.println("INVALID PM DATA");
    showLine(2, "Status: INVALID");

    delay(READ_INTERVAL);
    return;
  }

  lastValidRead = millis();

  // STEP 4: Threshold Detection
  const char* statusText;

  if (pm25 > PM25_CRITICAL_LIMIT) {
    statusText = "CRITICAL";
  } else if (pm25 > PM25_WARNING_LIMIT) {
    statusText = "WARNING";
  } else {
    statusText = "NORMAL";
  }

  // STEP 5: Update LCD
  char line[21];

  showLine(0, "EcoDorm Air Quality");

  snprintf(line, sizeof(line),
           "PM2.5: %d ug/m3", pm25);
  showLine(1, line);

  snprintf(line, sizeof(line),
           "Status: %s", statusText);
  showLine(2, line);

  snprintf(line, sizeof(line),
           "PM1:%d PM10:%d", pm1, pm10);
  showLine(3, line);

  // STEP 6: Serial Monitor
  Serial.println("----------------------");

  Serial.print("PM1.0: ");
  Serial.println(pm1);

  Serial.print("PM2.5: ");
  Serial.println(pm25);

  Serial.print("PM10: ");
  Serial.println(pm10);

  Serial.print("STATUS: ");
  Serial.println(statusText);

  // STEP 7: Check Wi-Fi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected");
    delay(READ_INTERVAL);
    return;
  }

  // STEP 8: Check NTP synchronization
  if (time(nullptr) < 1700000000) {
    Serial.println("Waiting for NTP time sync...");
    delay(READ_INTERVAL);
    return;
  }

  // STEP 9: Send HTTPS every 1 minute
  if (firstSend ||
      millis() - lastSend >= SEND_INTERVAL) {

    firstSend = false;
    lastSend = millis();

    sendToServer(
      pm1,
      pm25,
      pm10,
      statusText
    );
  }

  delay(READ_INTERVAL);
}
