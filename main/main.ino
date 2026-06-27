#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

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

Adafruit_BMP280 bmp;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool bmpOk = false;
bool oledOk = false;
uint8_t bmpAddrUsed = 0;
uint8_t oledAddrUsed = 0;
unsigned long lastUpdate = 0;

bool hasLastTemp = false;
float lastTempRounded = 0.0f;

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
  Serial.println("[scan] scanning i2c bus");
  int found = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.print("[scan] found device at 0x");
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
      found++;
    }
  }

  if (found == 0) {
    Serial.println("[scan] no devices found");
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
  Serial.println("[oled] probing addresses");

  if (i2cExists(OLED_ADDR1)) {
    oledAddrUsed = OLED_ADDR1;
  } else if (i2cExists(OLED_ADDR2)) {
    oledAddrUsed = OLED_ADDR2;
  } else {
    oledAddrUsed = 0;
  }

  if (oledAddrUsed == 0) {
    Serial.println("[oled] no oled found at 0x3c or 0x3d");
    oledOk = false;
    return;
  }

  Serial.print("[oled] trying 0x");
  if (oledAddrUsed < 16) Serial.print('0');
  Serial.println(oledAddrUsed, HEX);

  oledOk = display.begin(SSD1306_SWITCHCAPVCC, oledAddrUsed);
  Serial.print("[oled] begin result=");
  Serial.println(oledOk ? "ok" : "fail");

  if (oledOk) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("oled ok");
    display.print("addr 0x");
    if (oledAddrUsed < 16) display.print('0');
    display.println(oledAddrUsed, HEX);
    display.display();
    delay(1000);
  }
}

void setupBMP() {
  Serial.println("[bmp] trying 0x76");
  if (bmp.begin(BMP_ADDR1)) {
    bmpOk = true;
    bmpAddrUsed = BMP_ADDR1;
  } else {
    Serial.println("[bmp] 0x76 failed");
    Serial.println("[bmp] trying 0x77");
    if (bmp.begin(BMP_ADDR2)) {
      bmpOk = true;
      bmpAddrUsed = BMP_ADDR2;
    }
  }

  Serial.print("[bmp] begin result=");
  Serial.println(bmpOk ? "ok" : "fail");

  if (!bmpOk) return;

  Serial.print("[bmp] using addr 0x");
  if (bmpAddrUsed < 16) Serial.print('0');
  Serial.println(bmpAddrUsed, HEX);

  bmp.setSampling(
    Adafruit_BMP280::MODE_FORCED,
    Adafruit_BMP280::SAMPLING_X1,
    Adafruit_BMP280::SAMPLING_X1,
    Adafruit_BMP280::FILTER_OFF,
    Adafruit_BMP280::STANDBY_MS_1
  );

  Serial.println("[bmp] sampling configured: forced mode");
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
    Serial.print("[trend] first temp=");
    Serial.println(tempRounded, 1);
    return;
  }

  if (tempRounded > lastTempRounded) {
    setLeds(true, false);    // red on
    Serial.print("[trend] up ");
    Serial.print(lastTempRounded, 1);
    Serial.print(" -> ");
    Serial.println(tempRounded, 1);
  } else if (tempRounded < lastTempRounded) {
    setLeds(false, true);    // green on
    Serial.print("[trend] down ");
    Serial.print(lastTempRounded, 1);
    Serial.print(" -> ");
    Serial.println(tempRounded, 1);
  } else {
    setLeds(false, false);   // both off
    Serial.print("[trend] same ");
    Serial.println(tempRounded, 1);
  }

  lastTempRounded = tempRounded;
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("================================");
  Serial.println("[boot]")
  Serial.println("================================");

  Serial.print("[boot] i2c sda=");
  Serial.print(I2C_SDA);
  Serial.print(" scl=");
  Serial.println(I2C_SCL);

  Wire.begin(I2C_SDA, I2C_SCL, 100000);
  delay(50);

  setupLeds();
  scanI2C();
  setupOLED();
  setupBMP();

  if (!bmpOk && oledOk) {
    drawStatus("bmp not found", "wiggle it");
  } else if (bmpOk && oledOk) {
    drawStatus("sensors ready");
  }

  if (!oledOk) Serial.println("[oled] display unavailable");
  if (!bmpOk) Serial.println("[bmp] sensor unavailable");
}

void loop() {
  if (!bmpOk) {
    static unsigned long lastWarn = 0;
    if (millis() - lastWarn > 2000) {
      lastWarn = millis();
      Serial.println("[loop] bmp not ready");
    }
    delay(10);
    return;
  }

  if (millis() - lastUpdate < 1000) return;
  lastUpdate = millis();

  bmp.takeForcedMeasurement();
  delay(10);

  float rawTempC = bmp.readTemperature();
  float tempC = rawTempC + TEMP_OFFSET_C;
  float pressureHpa = bmp.readPressure() / 100.0f;
  float altitudeM = bmp.readAltitude(SEALEVEL_HPA);

  Serial.print("[data] temp_c=");
  Serial.print(tempC, 2);
  Serial.print(" temp_1dp=");
  Serial.print(round1dp(tempC), 1);
  Serial.print(" pressure_hpa=");
  Serial.print(pressureHpa, 2);
  Serial.print(" altitude_m=");
  Serial.println(altitudeM, 1);

  updateTempLeds(tempC);

  if (!oledOk) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

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