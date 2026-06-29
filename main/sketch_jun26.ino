#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include "secrets.h"
#define I2C_SDA 25
#define I2C_SCL 26

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1

#define BMP_ADDR1 0x76
#define BMP_ADDR2 0x77
#define OLED_ADDR1 0x3C
#define OLED_ADDR2 0x3D

#define SEALEVEL_HPA 1013.25f
#define TEMP_OFFSET_C 0.0f

#define LED_RED_PIN 12
#define LED_GREEN_PIN 14

#define WIFI_SSID "edge"

#define TZ_INFO "UTC"
#define DEVICE_NAME "esp32-bmp280-01"

#define SENSOR_INTERVAL_MS 1000
#define FLUSH_INTERVAL_MS 15000
#define WIFI_RETRY_MS 5000

Adafruit_BMP280 bmp;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

InfluxDBClient influx(
  INFLUXDB_URL,
  INFLUXDB_ORG,
  INFLUXDB_BUCKET,
  INFLUXDB_TOKEN,
  InfluxDbCloud2CACert
);

Point sensor("env");

bool bmpOk = false;
bool oledOk = false;
uint8_t bmpAddrUsed = 0;
uint8_t oledAddrUsed = 0;

bool hasLastTemp = false;
float lastTempRounded = 0.0f;

unsigned long lastSensorRead = 0;
unsigned long lastFlush = 0;
unsigned long lastWifiRetry = 0;

bool i2cExists(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

float round1dp(float x) {
  return roundf(x * 10.0f) / 10.0f;
}

void setLeds(bool redOn, bool greenOn) {
  digitalWrite(LED_RED_PIN, redOn ? HIGH : LOW);
  digitalWrite(LED_GREEN_PIN, greenOn ? HIGH : LOW);
}

void scanI2C() {
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("[scan] found 0x");
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
    }
  }
}

void drawStatus(const char* line1, const char* line2 = "") {
  if (!oledOk) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("env monitor");
  if (line1[0] != '\0') display.println(line1);
  if (line2[0] != '\0') display.println(line2);
  display.display();
}

void setupOLED() {
  if (i2cExists(OLED_ADDR1)) oledAddrUsed = OLED_ADDR1;
  else if (i2cExists(OLED_ADDR2)) oledAddrUsed = OLED_ADDR2;
  else oledAddrUsed = 0;

  if (!oledAddrUsed) return;
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, oledAddrUsed);
}

void setupBMP() {
  if (bmp.begin(BMP_ADDR1)) {
    bmpOk = true;
    bmpAddrUsed = BMP_ADDR1;
  } else if (bmp.begin(BMP_ADDR2)) {
    bmpOk = true;
    bmpAddrUsed = BMP_ADDR2;
  }

  if (!bmpOk) return;

  bmp.setSampling(
    Adafruit_BMP280::MODE_FORCED,
    Adafruit_BMP280::SAMPLING_X1,
    Adafruit_BMP280::SAMPLING_X1,
    Adafruit_BMP280::FILTER_OFF,
    Adafruit_BMP280::STANDBY_MS_1
  );
}

void setupLeds() {
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  setLeds(false, false);
}

void updateTempLeds(float tempC) {
  float tempRounded = round1dp(tempC);

  if (!hasLastTemp) {
    lastTempRounded = tempRounded;
    hasLastTemp = true;
    setLeds(false, false);
    return;
  }

  if (tempRounded > lastTempRounded) setLeds(true, false);
  else if (tempRounded < lastTempRounded) setLeds(false, true);
  else setLeds(false, false);

  lastTempRounded = tempRounded;
}

void connectWiFi() {
  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("[wifi] status=");
  Serial.println(WiFi.status());
  Serial.print("[wifi] local ip=");
  Serial.println(WiFi.localIP());
}

void reconnectWiFiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWifiRetry < WIFI_RETRY_MS) return;

  lastWifiRetry = millis();
  Serial.println("[wifi] reconnecting...");
  WiFi.disconnect(true);
  delay(50);
  WiFi.begin(WIFI_SSID);
}

void setupInflux() {
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  influx.setWriteOptions(
    WriteOptions()
      .batchSize(10)
      .bufferSize(50)
      .flushInterval(15)
      .retryInterval(5)
      .maxRetryAttempts(3)
  );

  sensor.addTag("device", DEVICE_NAME);
  sensor.addTag("sensor", "bmp280");

  if (influx.validateConnection()) {
    Serial.print("[influx] connected: ");
    Serial.println(influx.getServerUrl());
  } else {
    Serial.print("[influx] connection failed: ");
    Serial.println(influx.getLastErrorMessage());
  }
}

void queueSensorPoint(float tempC, float pressureHpa, float altitudeM) {
  sensor.clearFields();
  sensor.addField("temp_c", tempC);
  sensor.addField("temp_1dp", round1dp(tempC));
  sensor.addField("pressure_hpa", pressureHpa);
  sensor.addField("altitude_m", altitudeM);
  sensor.addField("wifi_rssi", WiFi.RSSI());

  if (!influx.writePoint(sensor)) {
    Serial.print("[influx] queue/write failed: ");
    Serial.println(influx.getLastErrorMessage());
  }
}

void flushInfluxIfNeeded() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastFlush < FLUSH_INTERVAL_MS) return;

  lastFlush = millis();

  if (!influx.flushBuffer()) {
    Serial.print("[influx] flush failed: ");
    Serial.println(influx.getLastErrorMessage());
  } else {
    Serial.println("[influx] flush ok");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(I2C_SDA, I2C_SCL, 100000);
  setupLeds();
  scanI2C();
  setupOLED();
  setupBMP();
  connectWiFi();
  setupInflux();

  if (!bmpOk && oledOk) drawStatus("bmp not found");
  else if (bmpOk && oledOk) drawStatus("ready");
}

void loop() {
  reconnectWiFiIfNeeded();

  if (!bmpOk) {
    delay(50);
    return;
  }

  unsigned long now = millis();
  if (now - lastSensorRead < SENSOR_INTERVAL_MS) {
    flushInfluxIfNeeded();
    delay(5);
    return;
  }

  lastSensorRead = now;

  bmp.takeForcedMeasurement();
  delay(10);

  float tempC = bmp.readTemperature() + TEMP_OFFSET_C;
  float pressureHpa = bmp.readPressure() / 100.0f;
  float altitudeM = bmp.readAltitude(SEALEVEL_HPA);

  updateTempLeds(tempC);
  queueSensorPoint(tempC, pressureHpa, altitudeM);

  if (oledOk) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.print("T ");
    display.print(tempC, 1);
    display.println(" C");

    display.setCursor(0, 11);
    display.print("P ");
    display.print(pressureHpa, 0);
    display.println(" hPa");

    display.setCursor(0, 22);
    display.print("A ");
    display.print(altitudeM, 0);
    display.println(" m");

    display.display();
  }

  flushInfluxIfNeeded();
}