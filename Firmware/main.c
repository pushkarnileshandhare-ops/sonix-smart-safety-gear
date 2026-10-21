
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_VEML6075.h>
#include <math.h>

// =====================================================
// Project : SONiX - Smart Connected Safety Gear
// Project ID : SIH26243
// Module : Welding Worker Safety Monitor
// Platform : ESP32
// =====================================================

// ---------------- Wi-Fi Configuration ----------------
const char* apSSID = "SIH26243_Safety";
const char* apPassword = "weldsafe123";

WebServer server(80);

// ---------------- Pin Configuration ------------------
#define I2C_SDA       8
#define I2C_SCL       9
#define MQ7_PIN       1

#define TEMP_LED      3
#define IMU_LED       4
#define GAS_LED       5
#define UV_LED        6
#define BUZZER_PIN    10

// ---------------- Alert Thresholds -------------------
const float TEMP_WARNING = 45.0;
const float TEMP_CRITICAL = 60.0;

const float IMU_Y_THRESHOLD = -8.0;

const int GAS_WARNING_ADC = 1800;
const int GAS_CRITICAL_ADC = 3000;

const float UV_WARNING_INDEX = 3.0;
const float UV_CRITICAL_INDEX = 8.0;

const unsigned long ALERT_DELAY = 5000;
const unsigned long SAMPLE_INTERVAL = 250;

// ---------------- Sensor Objects ---------------------
Adafruit_MLX90614 mlx;
Adafruit_MPU6050 mpu;
Adafruit_VEML6075 uv;

// ---------------- Sensor Availability ----------------
bool mlxOK = false;
bool mpuOK = false;
bool uvOK = false;

// ---------------- Sensor Readings --------------------
float objectTemp = NAN;
float ambientTemp = NAN;
float accY = NAN;
float uva = NAN;
float uvb = NAN;
float uvIndex = NAN;

int gasValue = 0;

// ---------------- Alert States -----------------------
bool tempAlert = false;
bool imuAlert = false;
bool gasAlert = false;
bool uvAlert = false;

bool tempCritical = false;
bool gasCritical = false;
bool uvCritical = false;

// ---------------- Alert Timers -----------------------
unsigned long tempStart = 0;
unsigned long imuStart = 0;
unsigned long gasStart = 0;
unsigned long uvStart = 0;

unsigned long lastSample = 0;
unsigned long lastBuzzerToggle = 0;

bool buzzerState = false;

// =====================================================
// Alert Persistence Logic
// =====================================================
void updateAlert(bool condition, bool &alert,
                 unsigned long &startTime) {
  unsigned long now = millis();

  if (condition) {
    if (startTime == 0) {
      startTime = now;
    }

    if (now - startTime >= ALERT_DELAY) {
      alert = true;
    }
  } else {
    startTime = 0;
    alert = false;
  }
}

// =====================================================
// Sensor Acquisition and Alert Processing
// =====================================================
void readSensors() {
  if (mlxOK) {
    float t = mlx.readObjectTempC();
    float a = mlx.readAmbientTempC();

    if (isfinite(t) && t > -70 && t < 400) {
      objectTemp = t;
    }

    if (isfinite(a) && a > -70 && a < 150) {
      ambientTemp = a;
    }
  }

  if (mpuOK) {
    sensors_event_t accel, gyro, sensorTemp;
    mpu.getEvent(&accel, &gyro, &sensorTemp);
    accY = accel.acceleration.y;
  }

  if (uvOK) {
    float a = uv.readUVA();
    float b = uv.readUVB();
    float i = uv.readUVI();

    if (isfinite(a)) uva = a;
    if (isfinite(b)) uvb = b;
    if (isfinite(i)) uvIndex = i;
  }

  gasValue = analogRead(MQ7_PIN);

  // Critical conditions
  gasCritical = gasValue >= GAS_CRITICAL_ADC;

  tempCritical = mlxOK && isfinite(objectTemp) &&
                 objectTemp >= TEMP_CRITICAL;

  uvCritical = uvOK && isfinite(uvIndex) &&
               uvIndex >= UV_CRITICAL_INDEX;

  // Temperature warning
  updateAlert(
    mlxOK && isfinite(objectTemp) &&
    objectTemp > TEMP_WARNING && !tempCritical,
    tempAlert, tempStart
  );

  // Provisional motion threshold
  updateAlert(
    mpuOK && isfinite(accY) &&
    accY > IMU_Y_THRESHOLD,
    imuAlert, imuStart
  );

  // Gas sensor ADC warning
  updateAlert(
    gasValue > GAS_WARNING_ADC && !gasCritical,
    gasAlert, gasStart
  );

  // UV warning
  updateAlert(
    uvOK && isfinite(uvIndex) &&
    uvIndex > UV_WARNING_INDEX && !uvCritical,
    uvAlert, uvStart
  );

  // Independent visual indicators
  digitalWrite(TEMP_LED,
               (tempAlert || tempCritical) ? HIGH : LOW);

  digitalWrite(IMU_LED, imuAlert ? HIGH : LOW);

  digitalWrite(GAS_LED,
               (gasAlert || gasCritical) ? HIGH : LOW);

  digitalWrite(UV_LED,
               (uvAlert || uvCritical) ? HIGH : LOW);

  // Serial diagnostics
  Serial.printf(
    "Object Temp: %.2f C | Ambient Temp: %.2f C | "
    "AccY: %.2f | MQ7 ADC: %d | UVA: %.2f | "
    "UVB: %.2f | UV Index: %.2f\n",
    objectTemp, ambientTemp, accY, gasValue,
    uva, uvb, uvIndex
  );
}

// =====================================================
// System Alert Classification
// =====================================================
int getLevel() {
  if (tempCritical || gasCritical || uvCritical) {
    return 3;
  }

  if (tempAlert || imuAlert || gasAlert || uvAlert) {
    return 2;
  }

  return 1;
}

String levelName(int level) {
  switch (level) {
    case 3: return "CRITICAL";
    case 2: return "WARNING";
    default: return "NORMAL";
  }
}

String levelColor(int level) {
  switch (level) {
    case 3: return "#B91C1C";
    case 2: return "#C2410C";
    default: return "#15803D";
  }
}

String sensorStatus(bool critical, bool warning) {
  if (critical) return "CRITICAL";
  if (warning) return "WARNING";
  return "NORMAL";
}

String sensorColor(bool critical, bool warning) {
  if (critical) return "#B91C1C";
  if (warning) return "#C2410C";
  return "#15803D";
}

String numberText(float value, int decimals = 2) {
  if (!isfinite(value)) return "Unavailable";
  return String(value, decimals);
}

// =====================================================
// Dashboard Sensor Card Generator
// =====================================================
String makeCard(String title, String value,
                bool warning, bool critical) {
  String state = sensorStatus(critical, warning);
  String color = sensorColor(critical, warning);

  String html = "<div class='card'><h3>" + title +
    "</h3><div class='value'>" + value +
    "</div><div class='status' style='background:" +
    color + "'>" + state + "</div></div>";

  return html;
}

// =====================================================
// Web Dashboard
// =====================================================
void handleRoot() {
  int level = getLevel();

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="2">
<title>SONiX | Worker Safety Monitor</title>
<style>
body {
  font-family: Arial, sans-serif;
  background: #101820;
  color: #ffffff;
  text-align: center;
  padding: 12px;
}
h1 { color: #00d4ff; }
.banner {
  padding: 18px;
  border-radius: 12px;
  margin: 18px auto;
  max-width: 900px;
}
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit,minmax(220px,1fr));
  gap: 12px;
  max-width: 950px;
  margin: 20px auto;
}
.card {
  background: #202d39;
  padding: 18px;
  border-radius: 12px;
}
.value {
  font-size: 23px;
  margin: 14px 0;
  overflow-wrap: anywhere;
}
.status {
  color: white;
  padding: 9px;
  border-radius: 7px;
  font-weight: bold;
}
.small {
  color: #bbc5ce;
  font-size: 13px;
}
</style>
</head>
<body>
<h1>SONiX</h1>
<h2>Smart Connected Worker Safety Monitor</h2>
)rawliteral";

  html += "<div class='banner' style='background:" +
          levelColor(level) + "'>";

  html += "<h2>LEVEL " + String(level) + " - " +
          levelName(level) + "</h2>";

  if (level == 3) {
    html += "<p>Critical condition detected. Take protective action.</p>";
  } else if (level == 2) {
    html += "<p>Warning detected. Inspect the affected sensor.</p>";
  } else {
    html += "<p>No configured alert condition is active.</p>";
  }

  html += "</div><div class='grid'>";

  html += makeCard(
    "Object Temperature",
    numberText(objectTemp) + " &deg;C",
    tempAlert, tempCritical
  );

  html += makeCard(
    "IMU Y Acceleration",
    numberText(accY) + " m/s&sup2;",
    imuAlert, false
  );

  html += makeCard(
    "MQ-7 Gas Sensor ADC",
    String(gasValue),
    gasAlert, gasCritical
  );

  html += makeCard(
    "UV Index",
    numberText(uvIndex),
    uvAlert, uvCritical
  );

  html += makeCard(
    "UVA Reading",
    numberText(uva),
    uvAlert, uvCritical
  );

  html += makeCard(
    "UVB Reading",
    numberText(uvb),
    uvAlert, uvCritical
  );

  html += "</div>";

  html += "<p class='small'>Dashboard refresh interval: 2 seconds</p>";
  html += "<p class='small'>Prototype firmware. Thresholds require calibration.</p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// =====================================================
// Audible Alert Management
// =====================================================
void updateBuzzer() {
  int level = getLevel();
  unsigned long now = millis();

  if (level == 1) {
    buzzerState = false;
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  // Critical alert: continuous buzzer
  if (level == 3) {
    buzzerState = true;
    digitalWrite(BUZZER_PIN, HIGH);
    return;
  }

  // Warning alert: intermittent buzzer
  if (now - lastBuzzerToggle >= 400) {
    lastBuzzerToggle = now;
    buzzerState = !buzzerState;
    digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
  }
}

// =====================================================
// System Initialization
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(TEMP_LED, OUTPUT);
  pinMode(IMU_LED, OUTPUT);
  pinMode(GAS_LED, OUTPUT);
  pinMode(UV_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(TEMP_LED, LOW);
  digitalWrite(IMU_LED, LOW);
  digitalWrite(GAS_LED, LOW);
  digitalWrite(UV_LED, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  analogReadResolution(12);

  // Initialize I2C bus
  Wire.begin(I2C_SDA, I2C_SCL);

  // Initialize temperature sensor
  mlxOK = mlx.begin(0x5A, &Wire);
  Serial.println(mlxOK ? "MLX90614 connected" :
                         "MLX90614 not detected");

  // Initialize motion sensor
  mpuOK = mpu.begin(0x68, &Wire);
  Serial.println(mpuOK ? "MPU6050 connected" :
                         "MPU6050 not detected");

  if (mpuOK) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  // Initialize UV sensor
  uvOK = uv.begin(&Wire);
  Serial.println(uvOK ? "VEML6075 connected" :
                        "VEML6075 not detected");

  if (uvOK) {
    uv.setIntegrationTime(VEML6075_100MS);
    uv.setHighDynamic(false);
    uv.setForcedMode(false);
  }

  // Start Wi-Fi access point
  WiFi.mode(WIFI_AP);

  if (WiFi.softAP(apSSID, apPassword)) {
    Serial.println("Wi-Fi access point started");
    Serial.print("SSID: ");
    Serial.println(apSSID);
    Serial.println("Password: [configured]");
    Serial.print("Dashboard URL: http://");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Wi-Fi access point failed");
  }

  // Configure web server
  server.on("/", HTTP_GET, handleRoot);
  server.begin();

  Serial.println("SONiX Safety Monitor initialized");
}

// =====================================================
// Main Execution Loop
// =====================================================
void loop() {
  server.handleClient();

  unsigned long now = millis();

  if (now - lastSample >= SAMPLE_INTERVAL) {
    lastSample = now;
    readSensors();
  }

  updateBuzzer();

  delay(2);
}
