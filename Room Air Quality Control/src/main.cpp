#define BLYNK_TEMPLATE_ID "TMPL661tSdNYO"
#define BLYNK_TEMPLATE_NAME "Room Air Quality Control"
#define BLYNK_AUTH_TOKEN "Pen0YKbtSqQ3m_Sh-MmSrMHpUJgKvKX7"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <DHT.h>
#include <Stepper.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BlynkSimpleEsp32.h>
#include <math.h>

// WiFi credentials
const char *wifiSsid = "Wokwi-GUEST";
const char *wifiPassword = "";

// Pin and sensor setup
const int ledPin = 27;
const int dhtPin = 13;
const int mq2AnalogPin = 36;

const int stepsPerRevolution = 200;
Stepper myStepper(stepsPerRevolution, 17, 5, 18, 19);

// Blynk virtual pins
#define controlVpin V0
#define statusVpin V1
#define tempVpin V2
#define humVpin V3
#define gasVpin V4
#define tempMinVpin V5
#define tempMaxVpin V6
#define humMinVpin V7
#define humMaxVpin V8
#define gasMaxVpin V9
#define roomLengthVpin V10
#define roomWidthVpin V11
#define roomHeightVpin V12

// OLED screen setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// DHT sensor
DHT dhtSensor(dhtPin, DHT22);

// Timing and control
const int sensorReadingInterval = 2000;
unsigned long lastSensorReadingTime = 0;
bool activeStatus = false;
bool motorShouldRun = false;

// Default thresholds and room dimensions
float tempMin = 20;
float tempMax = 30;
float humMin = 45;
float humMax = 60;
float gasMax = 500;

float roomLength = 3.0;
float roomWidth = 3.0;
float roomHeight = 2.5;
float currentStepperRPM = 400;

void updateStepperSpeedByRoomVolume()
{
  float volume = roomLength * roomWidth * roomHeight;
  float baseVolume = 22.5;
  float baseRPM = 400.0;
  currentStepperRPM = baseRPM * cbrt(volume / baseVolume);
  currentStepperRPM = constrain(currentStepperRPM, 100, 2000);
  myStepper.setSpeed(currentStepperRPM);
}

void showStatus(bool status)
{
  digitalWrite(ledPin, status ? HIGH : LOW);
  Blynk.virtualWrite(statusVpin, status);

  if (status)
  {
    Serial.println("Status: ON");
  }
  else
  {
    Serial.println("Status: OFF");
  }
}

BLYNK_CONNECTED()
{
  Blynk.syncVirtual(controlVpin, tempMinVpin, tempMaxVpin, humMinVpin, humMaxVpin, gasMaxVpin,
                    roomLengthVpin, roomWidthVpin, roomHeightVpin);
}

BLYNK_WRITE(controlVpin)
{
  activeStatus = param.asInt() == 1;
  showStatus(activeStatus);

  if (!activeStatus)
  {
    digitalWrite(ledPin, LOW);
    motorShouldRun = false;
    display.clearDisplay();
    display.display(); // Blank screen
  }
}

BLYNK_WRITE(tempMinVpin) { tempMin = param.asFloat(); }
BLYNK_WRITE(tempMaxVpin) { tempMax = param.asFloat(); }
BLYNK_WRITE(humMinVpin) { humMin = param.asFloat(); }
BLYNK_WRITE(humMaxVpin) { humMax = param.asFloat(); }
BLYNK_WRITE(gasMaxVpin) { gasMax = param.asFloat(); }

BLYNK_WRITE(roomLengthVpin)
{
  roomLength = param.asFloat();
  updateStepperSpeedByRoomVolume();
}
BLYNK_WRITE(roomWidthVpin)
{
  roomWidth = param.asFloat();
  updateStepperSpeedByRoomVolume();
}
BLYNK_WRITE(roomHeightVpin)
{
  roomHeight = param.asFloat();
  updateStepperSpeedByRoomVolume();
}

void setup()
{
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  pinMode(mq2AnalogPin, INPUT);
  dhtSensor.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    while (1)
      ; // halt
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Screen Status: OFF");
  display.display();

  Blynk.begin(BLYNK_AUTH_TOKEN, wifiSsid, wifiPassword);
  updateStepperSpeedByRoomVolume();
}

void loop()
{
  Blynk.run();

  // If OFF, stop everything
  if (!activeStatus)
    return;

  // Keep motor spinning if air is not safe
  if (motorShouldRun)
  {
    myStepper.step(stepsPerRevolution);
  }

  // Sensor read every 2s
  unsigned long currentTime = millis();
  if (currentTime - lastSensorReadingTime >= sensorReadingInterval)
  {
    lastSensorReadingTime = currentTime;

    float temp = dhtSensor.readTemperature();
    float hum = dhtSensor.readHumidity();
    float gasRaw = analogRead(mq2AnalogPin);
    float gasPPM = (gasRaw - 843) / (4041.0 - 843) * 1000.0;

    Serial.printf("Temperature: %.2f°C, Humidity: %.2f%%, Gas: %.2f ppm\n", temp, hum, gasPPM);

    Blynk.virtualWrite(tempVpin, temp);
    Blynk.virtualWrite(humVpin, hum);
    Blynk.virtualWrite(gasVpin, gasPPM);

    bool tempSafe = temp >= tempMin && temp <= tempMax;
    bool humSafe = hum >= humMin && hum <= humMax;
    bool gasSafe = gasPPM <= gasMax;

    motorShouldRun = !(tempSafe && humSafe && gasSafe);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.println("Screen Status: ON");

    if (!motorShouldRun)
    {
      Serial.println("Air Quality SAFE");
      display.println("Air Quality SAFE");
      display.print("Motor OFF (Idle)");
    }
    else
    {
      Serial.print("Air Quality BAD - Cause: ");
      display.println("Air Quality BAD");

      if (!tempSafe)
      {
        Serial.print("Temp ");
        display.println("Problem: Temp");
      }
      if (!humSafe)
      {
        Serial.print("Hum ");
        display.println("Problem: Hum");
      }
      if (!gasSafe)
      {
        Serial.print("Gas ");
        display.println("Problem: Gas");
      }

      display.println("Motor ACTIVE");
      Serial.println();
    }
    display.display();
  }
}
