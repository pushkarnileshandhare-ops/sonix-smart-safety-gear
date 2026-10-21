#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <math.h>

#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_MLX90614.h>
#include <Adafruit_VEML6075.h>

// ---------------- USER SETTINGS ----------------

const char* WIFI_SSID = "SIH26243_Safety";
const char* WIFI_PASSWORD = "weldsafe123";

const char* WORKER_ID = "W001";
const char* LOG_FILE = "/hazard_log.csv";

// ---------------- PIN CONFIGURATION ----------------

// Analog gas sensors
#define MQ7_PIN    0
#define MQ135_PIN  1

// Buzzer
#define BUZZER_PIN 2

// LEDs
#define TEMP_LED 3
#define SDA_PIN  4
#define SCL_PIN  5
#define IMU_LED  6
#define GAS_LED  7
#define UV_LED   10

// I2C sensors: GPIO 4 SDA, GPIO 5 SCL

// MicroSD SPI pins
#define SD_MISO_PIN 18
#define SD_MOSI_PIN 19
#define SD_CS_PIN   20
#define SD_SCK_PIN  21

// ---------------- THRESHOLDS ----------------

// Prototype values; calibrate before real use
const float TEMP_WARNING = 45.0;
const float TEMP_CRITICAL = 60.0;

const float IMU_WARNING = 8.0;
const float IMU_CRITICAL = 12.0;

// MQ-7 ADC thresholds
const int GAS_WARNING_ADC = 1800;
const int GAS_CRITICAL_ADC = 3000;

// MQ-135 ADC thresholds
const int MQ135_WARNING_ADC = 1800;
const int MQ135_CRITICAL_ADC = 3000;

const float UV_WARNING = 3.0;
const float UV_CRITICAL = 8.0;

const unsigned long WARNING_DELAY_MS = 5000;

// ---------------- OBJECTS ----------------

WebServer server(80);

Adafruit_MPU6050 mpu;
Adafruit_MLX90614 mlx;
Adafruit_VEML6075 uv;

// ---------------- SENSOR STATUS ----------------

bool mpuOK = false;
bool mlxOK = false;
bool uvOK = false;
bool mq135OK = true;
bool sdOK = false;

// Current readings
float temperatureC = NAN;
float accY = NAN;

int gasADC = 0;
int mq135ADC = 0;

float uvi = NAN;

// ---------------- HAZARD STATE ----------------

struct HazardState {
  bool active = false;
  bool timing = false;
  unsigned long conditionStart = 0;
  int level = 0;

  const char* eventName;
  uint8_t ledPin;
};

HazardState tempHazard = {
  false, false, 0, 0,
  "HIGH_TEMPERATURE", TEMP_LED
};

HazardState imuHazard = {
  false, false, 0, 0,
  "IMU_HAZARD", IMU_LED
};

HazardState gasHazard = {
  false, false, 0, 0,
  "GAS_HAZARD", GAS_LED
};

HazardState uvHazard = {
  false, false, 0, 0,
  "UV_HAZARD", UV_LED
};

HazardState mq135Hazard = {
  false, false, 0, 0,
  "AIR_QUALITY_HAZARD", GAS_LED
};

// ---------------- ELAPSED TIME ----------------

// Elapsed time since ESP32 startup.
// This is not a real date/time timestamp.

String elapsedTimestamp() {
  unsigned long seconds = millis() / 1000UL;

  unsigned long hours = seconds / 3600UL;
  unsigned long minutes = (seconds % 3600UL) / 60UL;
  unsigned long secs = seconds % 60UL;

  char buffer[24];

  snprintf(
    buffer,
    sizeof(buffer),
    "%02lu:%02lu:%02lu",
    hours,
    minutes,
    secs
  );

  return String(buffer);
}

// ---------------- READABLE STATUS ----------------

String severityName(int level) {
  if (level == 2) return "CRITICAL";
  if (level == 1) return "WARNING";

  return "NORMAL";
}

String wifiStatus() {
  if (WiFi.softAPgetStationNum() > 0) {
    return "CLIENT_CONNECTED";
  }

  return "NO_CLIENT";
}

bool anyHazardActive() {
  return tempHazard.active ||
         imuHazard.active ||
         gasHazard.active ||
         uvHazard.active ||
         mq135Hazard.active;
}

bool anyCritical() {
  return (tempHazard.active &&
          tempHazard.level == 2) ||

         (imuHazard.active &&
          imuHazard.level == 2) ||

         (gasHazard.active &&
          gasHazard.level == 2) ||

         (uvHazard.active &&
          uvHazard.level == 2) ||

         (mq135Hazard.active &&
          mq135Hazard.level == 2);
}

// ---------------- SD CARD LOGGING ----------------

void writeLogHeader() {
  if (!sdOK) return;

  if (!SD.exists(LOG_FILE)) {
    File file = SD.open(LOG_FILE, FILE_WRITE);

    if (file) {
      file.println(
        "Elapsed_Time,Worker_ID,Event,Severity,"
        "WiFi_Status,Temperature_C,AccelY_m_s2,"
        "MQ7_ADC,MQ135_ADC,UVI"
      );

      file.close();
    } else {
      Serial.println("Could not create log file.");
    }
  }
}

void logEvent(
  const String& eventName,
  const String& severity
) {
  String timestamp = elapsedTimestamp();
  String network = wifiStatus();

  // Event-only Serial logging
  Serial.println("------------------------------");
  Serial.println("Elapsed time: " + timestamp);
  Serial.println("Worker ID: " + String(WORKER_ID));
  Serial.println("Event: " + eventName);
  Serial.println("Severity: " + severity);
  Serial.println("Wi-Fi: " + network);

  Serial.println(
    "Temperature C: " + String(temperatureC, 2)
  );

  Serial.println(
    "Accel Y m/s2: " + String(accY, 2)
  );

  Serial.println(
    "MQ-7 ADC: " + String(gasADC)
  );

  Serial.println(
    "MQ-135 ADC: " + String(mq135ADC)
  );

  Serial.println(
    "UV Index: " + String(uvi, 2)
  );

  if (!sdOK) {
    Serial.println(
      "SD unavailable; event not saved."
    );

    return;
  }

  File file = SD.open(LOG_FILE, FILE_APPEND);

  if (!file) {
    Serial.println(
      "ERROR: Could not append event to SD."
    );

    return;
  }

  file.print(timestamp);
  file.print(",");
  file.print(WORKER_ID);
  file.print(",");
  file.print(eventName);
  file.print(",");
  file.print(severity);
  file.print(",");
  file.print(network);
  file.print(",");
  file.print(String(temperatureC, 2));
  file.print(",");
  file.print(String(accY, 2));
  file.print(",");
  file.print(gasADC);
  file.print(",");
  file.print(mq135ADC);
  file.print(",");
  file.println(String(uvi, 2));

  file.flush();
  file.close();

  Serial.println("Hazard event saved to SD.");
}

// ---------------- HAZARD STATE MACHINE ----------------

// Warning conditions must persist for 5 seconds.
// Critical conditions trigger immediately.
// Events are not repeatedly logged while active.

void updateHazard(
  HazardState& hazard,
  bool warningCondition,
  bool criticalCondition
) {
  unsigned long now = millis();

  if (criticalCondition) {
    hazard.timing = false;

    if (!hazard.active) {
      hazard.active = true;
      hazard.level = 2;

      logEvent(hazard.eventName, "CRITICAL");

    } else if (hazard.level < 2) {
      hazard.level = 2;

      logEvent(
        String(hazard.eventName) + "_ESCALATED",
        "CRITICAL"
      );
    }

    return;
  }

  if (warningCondition) {
    if (!hazard.active) {
      if (!hazard.timing) {
        hazard.timing = true;
        hazard.conditionStart = now;
      }

      if (
        now - hazard.conditionStart >=
        WARNING_DELAY_MS
      ) {
        hazard.active = true;
        hazard.level = 1;
        hazard.timing = false;

        logEvent(hazard.eventName, "WARNING");
      }

    } else {
      if (hazard.level == 2) {
        hazard.level = 1;

        logEvent(
          String(hazard.eventName) +
          "_LEVEL_REDUCED",
          "WARNING"
        );
      }
    }

    return;
  }

  // Hazard condition has cleared
  hazard.timing = false;

  if (hazard.active) {
    hazard.active = false;
    hazard.level = 0;

    logEvent(
      String(hazard.eventName) + "_CLEARED",
      "NORMAL"
    );
  }
}

// ---------------- SENSOR READING ----------------

void readSensors() {
  if (mlxOK) {
    temperatureC = mlx.readObjectTempC();
  } else {
    temperatureC = NAN;
  }

  if (mpuOK) {
    sensors_event_t accelEvent;
    sensors_event_t gyroEvent;
    sensors_event_t tempEvent;

    mpu.getEvent(
      &accelEvent,
      &gyroEvent,
      &tempEvent
    );

    accY = accelEvent.acceleration.y;

  } else {
    accY = NAN;
  }

  gasADC = analogRead(MQ7_PIN);
  mq135ADC = analogRead(MQ135_PIN);

  if (uvOK) {
    uvi = uv.readUVI();
  } else {
    uvi = NAN;
  }
}

// ---------------- ALERT PROCESSING ----------------

void processHazards() {
  bool tempValid =
    mlxOK && isfinite(temperatureC);

  bool imuValid =
    mpuOK && isfinite(accY);

  bool uvValid =
    uvOK && isfinite(uvi);

  bool tempWarning =
    tempValid &&
    temperatureC >= TEMP_WARNING;

  bool tempCritical =
    tempValid &&
    temperatureC >= TEMP_CRITICAL;

  // Acceleration magnitude on Y axis
  float accMagnitudeY =
    imuValid ? fabsf(accY) : 0.0f;

  bool imuWarning =
    imuValid &&
    accMagnitudeY >= IMU_WARNING;

  bool imuCritical =
    imuValid &&
    accMagnitudeY >= IMU_CRITICAL;

  bool gasWarning =
    gasADC >= GAS_WARNING_ADC;

  bool gasCritical =
    gasADC >= GAS_CRITICAL_ADC;

  bool mq135Warning =
    mq135OK &&
    mq135ADC >= MQ135_WARNING_ADC;

  bool mq135Critical =
    mq135OK &&
    mq135ADC >= MQ135_CRITICAL_ADC;

  bool uvWarning =
    uvValid &&
    uvi >= UV_WARNING;

  bool uvCritical =
    uvValid &&
    uvi >= UV_CRITICAL;

  updateHazard(
    tempHazard,
    tempWarning,
    tempCritical
  );

  updateHazard(
    imuHazard,
    imuWarning,
    imuCritical
  );

  updateHazard(
    gasHazard,
    gasWarning,
    gasCritical
  );

  updateHazard(
    mq135Hazard,
    mq135Warning,
    mq135Critical
  );

  updateHazard(
    uvHazard,
    uvWarning,
    uvCritical
  );

  // Four LEDs total
  digitalWrite(
    TEMP_LED,
    tempHazard.active ? HIGH : LOW
  );

  digitalWrite(
    IMU_LED,
    imuHazard.active ? HIGH : LOW
  );

  // Shared gas LED for MQ-7 and MQ-135
  digitalWrite(
    GAS_LED,
    (gasHazard.active ||
     mq135Hazard.active) ? HIGH : LOW
  );

  digitalWrite(
    UV_LED,
    uvHazard.active ? HIGH : LOW
  );

  // Critical hazard: continuous buzzer
  // Warning hazard: intermittent buzzer
  if (anyCritical()) {
    digitalWrite(BUZZER_PIN, HIGH);

  } else if (anyHazardActive()) {
    digitalWrite(
      BUZZER_PIN,
      (millis() % 1000UL < 150UL)
        ? HIGH
        : LOW
    );

  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ---------------- DASHBOARD ----------------

String dashboardHTML() {
  String html = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport"
 content="width=device-width, initial-scale=1">
<meta http-equiv="refresh" content="2">
<title>ESP32 Safety Monitor</title>

<style>
body {
  font-family: Arial;
  margin: 18px;
  background: #f3f5f7;
  color: #222;
}
.card {
  background: white;
  padding: 14px;
  margin: 10px 0;
  border-radius: 10px;
}
h2 {
  margin-bottom: 4px;
}
</style>
</head>

<body>
<h2>Welding Safety Monitor</h2>

<div class="card">
<b>Worker ID:</b> WORKER_PLACEHOLDER<br>
<b>Elapsed time:</b> TIME_PLACEHOLDER<br>
<b>Dashboard client:</b> WIFI_PLACEHOLDER
</div>

<div class="card">
<b>Temperature:</b>
TEMP_PLACEHOLDER &deg;C<br>
<b>Temperature status:</b> TEMP_STATUS
</div>

<div class="card">
<b>Accelerometer Y:</b>
ACC_PLACEHOLDER m/s²<br>
<b>IMU status:</b> IMU_STATUS
</div>

<div class="card">
<b>MQ-7 ADC:</b> GAS_PLACEHOLDER<br>
<b>MQ-7 status:</b> GAS_STATUS<br>
<b>MQ-135 ADC:</b> MQ135_PLACEHOLDER<br>
<b>Air-quality status:</b> MQ135_STATUS
</div>

<div class="card">
<b>UV Index:</b> UV_PLACEHOLDER<br>
<b>UV status:</b> UV_STATUS
</div>

<div class="card">
<b>Overall status:</b> OVERALL_STATUS<br>
<b>SD card:</b> SD_STATUS
</div>

<p>
Prototype readings are not certified
occupational safety measurements.
</p>
</body>
</html>
)HTML";

  html.replace(
    "WORKER_PLACEHOLDER",
    WORKER_ID
  );

  html.replace(
    "TIME_PLACEHOLDER",
    elapsedTimestamp()
  );

  html.replace(
    "WIFI_PLACEHOLDER",
    wifiStatus()
  );

  html.replace(
    "TEMP_PLACEHOLDER",
    String(temperatureC, 2)
  );

  html.replace(
    "ACC_PLACEHOLDER",
    String(accY, 2)
  );

  html.replace(
    "GAS_PLACEHOLDER",
    String(gasADC)
  );

  html.replace(
    "MQ135_PLACEHOLDER",
    String(mq135ADC)
  );

  html.replace(
    "UV_PLACEHOLDER",
    String(uvi, 2)
  );

  html.replace(
    "TEMP_STATUS",
    tempHazard.active
      ? severityName(tempHazard.level)
      : "NORMAL"
  );

  html.replace(
    "IMU_STATUS",
    imuHazard.active
      ? severityName(imuHazard.level)
      : "NORMAL"
  );

  html.replace(
    "GAS_STATUS",
    gasHazard.active
      ? severityName(gasHazard.level)
      : "NORMAL"
  );

  html.replace(
    "MQ135_STATUS",
    mq135Hazard.active
      ? severityName(mq135Hazard.level)
      : "NORMAL"
  );

  html.replace(
    "UV_STATUS",
    uvHazard.active
      ? severityName(uvHazard.level)
      : "NORMAL"
  );

  String overall = "NORMAL";

  if (anyCritical()) {
    overall = "CRITICAL";

  } else if (anyHazardActive()) {
    overall = "WARNING";
  }

  html.replace("OVERALL_STATUS", overall);

  html.replace(
    "SD_STATUS",
    sdOK ? "READY" : "UNAVAILABLE"
  );

  return html;
}

// ---------------- SETUP ----------------

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

  // Start shared I2C bus
  Wire.begin(SDA_PIN, SCL_PIN);

  mlxOK = mlx.begin();

  Serial.println(
    mlxOK
      ? "MLX90614 ready"
      : "MLX90614 not found"
  );

  mpuOK = mpu.begin(0x68, &Wire);

  Serial.println(
    mpuOK
      ? "MPU6050 ready"
      : "MPU6050 not found"
  );

  if (mpuOK) {
    mpu.setAccelerometerRange(
      MPU6050_RANGE_8_G
    );

    mpu.setGyroRange(
      MPU6050_RANGE_500_DEG
    );

    mpu.setFilterBandwidth(
      MPU6050_BAND_21_HZ
    );
  }

  uvOK = uv.begin(&Wire);

  Serial.println(
    uvOK
      ? "VEML6075 ready"
      : "VEML6075 not found"
  );

  if (uvOK) {
    uv.setIntegrationTime(
      VEML6075_100MS
    );
  }

  // Initialize microSD SPI
  SPI.begin(
    SD_SCK_PIN,
    SD_MISO_PIN,
    SD_MOSI_PIN,
    SD_CS_PIN
  );

  sdOK = SD.begin(SD_CS_PIN, SPI);

  if (sdOK) {
    Serial.println("SD card ready");
    writeLogHeader();

  } else {
    Serial.println(
      "WARNING: SD card unavailable"
    );
  }

  // Start ESP32 Wi-Fi access point
  WiFi.mode(WIFI_AP);

  bool apOK = WiFi.softAP(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  if (apOK) {
    Serial.println(
      "Wi-Fi access point started"
    );

    Serial.print("Connect to: ");
    Serial.println(WIFI_SSID);

    Serial.print("Dashboard: http://");
    Serial.println(WiFi.softAPIP());

  } else {
    Serial.println(
      "Wi-Fi access point failed"
    );
  }

  server.on("/", []() {
    server.send(
      200,
      "text/html",
      dashboardHTML()
    );
  });

  server.begin();

  Serial.println(
    "Pin map loaded from wiring table."
  );

  Serial.println(
    "Safety monitor started."
  );

  Serial.println(
    "Worker ID: " + String(WORKER_ID)
  );

  Serial.println(
    "Logs are event-only."
  );
}

// ---------------- MAIN LOOP ----------------

void loop() {
  server.handleClient();

  readSensors();
  processHazards();

  delay(50);
}
