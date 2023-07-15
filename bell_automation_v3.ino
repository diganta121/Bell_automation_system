#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <SD.h>
#include <ArduinoJson.h>

RTC_DS1307 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);

const int bell = 5;
const int P = 17;
const int N = 16;
const int over_ride_off = 4;

const int setting_address = 0;

const int Lenght = 3;  // Bell ring length in seconds

const int chipSelect = 15;  // SD card chip select pin
const char* filename = "/bell_schedule.json";  // JSON file name

File configFile;

int H = 0;
int M = 0;
int S = 0;

boolean bell_status = true;
boolean Over_ride = true;

int currentAlarm = -1;
unsigned long previousMillis = 0;
unsigned long interval = 20 * 60 * 1000;  // Check alarm every 20 minutes

int h[16] = {0};
int m[16] = {0};
int numAlarms = 0;

const int statusLED = 13;      // Status LED pin
const int workingLED = 12;     // Working LED pin
const int heartbeatLED = 11;   // Heartbeat LED pin

const unsigned long checkInterval = 20 * 60 * 1000;  // Check nearest bell every 20 minutes
const unsigned long halfInterval = checkInterval / 2; // Time to display bell information when halfway to the bell

unsigned long lastCheckTime = 0;
unsigned long lastDisplayTime = 0;

bool powerSavingMode = false;

void setup() {
  lcd.begin(16, 2);
  lcd.init();
  lcd.backlight();

  pinMode(P, OUTPUT);
  pinMode(N, OUTPUT);
  pinMode(bell, OUTPUT);
  pinMode(over_ride_off, INPUT_PULLUP);

  digitalWrite(P, HIGH);
  digitalWrite(N, LOW);

  attachInterrupt(digitalPinToInterrupt(over_ride_off), over_ride, RISING);

  if (!rtc.begin()) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RTC initialization failed");
    while (true) {
      blinkStatusLED(500);
    }
  }

  if (!SD.begin(chipSelect)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Error: SD Card");
    lcd.setCursor(0, 1);
    lcd.print("initialization failed");
    while (true) {
      blinkStatusLED(500);
    }
  }

  loadBellSchedule();

  pinMode(statusLED, OUTPUT);
  pinMode(workingLED, OUTPUT);
  pinMode(heartbeatLED, OUTPUT);

  digitalWrite(statusLED, HIGH);      // Turn on the status LED to indicate program running
  digitalWrite(workingLED, HIGH);     // Turn on the working LED
  digitalWrite(heartbeatLED, LOW);    // Turn off the heartbeat LED
}

void loop() {
  unsigned long currentMillis = millis();

  if (!powerSavingMode) {
    // Update LCD every second
    if (currentMillis - lastDisplayTime >= 1000) {
      lastDisplayTime = currentMillis;
      updateLCD();
    }
  }

  // Check nearest bell every 20 minutes
  if (currentMillis - lastCheckTime >= checkInterval) {
    lastCheckTime = currentMillis;
    checkNearestBell();
  }

  // Check if halfway to the bell or override button is pressed
  if (!powerSavingMode && currentMillis - lastCheckTime >= halfInterval) {
    activatePowerSavingMode();
  }

  if (rtc.isrunning()) {
    DateTime now = rtc.now();
    H = now.hour();
    M = now.minute();
    S = now.second();

    if (H == 0 && M == 0 && S == 0) {
      digitalWrite(bell, LOW);
    }

    for (int alarm = 0; alarm < numAlarms; alarm++) {
      if (H == h[alarm] && M == m[alarm] && S == 0) {
        currentAlarm = alarm;
        digitalWrite(bell, HIGH);
        break;  // Exit the loop after the alarm is triggered
      }
    }
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RTC stopped!!!");
    lcd.setCursor(0, 1);
    lcd.print("Run SetTime code");
    blinkStatusLED(500);  // Blink status LED at 500ms intervals
    return;  // Exit the loop as there's an error with the RTC
  }

  delay(Lenght);
}

void over_ride() {
  lcd.clear();
  while (Over_ride) {
    digitalWrite(bell, HIGH);
    lcd.setCursor(0, 0);
    lcd.print("Bell Overridden");
    lcd.setCursor(0, 1);
    lcd.print("Press exit to quit");
    if (digitalRead(over_ride_off) == LOW) {
      Over_ride = false;
    }
  }
  digitalWrite(bell, LOW);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Bell is Enabled");
  lcd.setCursor(0, 1);
  lcd.print("Exit button pressed");
  delay(2000);
  lcd.clear();
  saveSettings();
}

void activatePowerSavingMode() {
  powerSavingMode = true;

  // Display bell information for 30 seconds
  unsigned long displayEndTime = lastCheckTime + 30 * 1000;
  while (millis() < displayEndTime) {
    displayTimeTillBell(h[currentAlarm] * 60 + m[currentAlarm]);
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Power Saving Mode");
  lcd.setCursor(0, 1);
  lcd.print("Press Exit to quit");

  // Check for exit button press to exit power saving mode
  while (true) {
    if (digitalRead(over_ride_off) == LOW) {
      powerSavingMode = false;
      break;
    }
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Bell is Enabled");
  lcd.setCursor(0, 1);
  lcd.print("Exit button pressed");
  delay(2000);
  lcd.clear();
  saveSettings();
}

void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Time:");
  lcd.print(formatDigits(H));
  lcd.print(":");
  lcd.print(formatDigits(M));
  lcd.print(":");
  lcd.print(formatDigits(S));

  lcd.setCursor(0, 1);
  lcd.print("Next Bell:");
  if (currentAlarm >= 0 && currentAlarm < numAlarms) {
    lcd.print(formatDigits(h[currentAlarm]));
    lcd.print(":");
    lcd.print(formatDigits(m[currentAlarm]));
  } else {
    lcd.print("--:--");
  }
}

void checkNearestBell() {
  unsigned long currentTime = rtc.now().unixtime();
  unsigned long nearestTimeDiff = ULONG_MAX;
  int nearestAlarmIndex = -1;

  for (int i = 0; i < numAlarms; i++) {
    DateTime alarmTime = DateTime(rtc.now().year(), rtc.now().month(), rtc.now().day(), h[i], m[i], 0);
    unsigned long alarmTimeDiff = alarmTime.unixtime() - currentTime;

    if (alarmTimeDiff > 0 && alarmTimeDiff < nearestTimeDiff) {
      nearestTimeDiff = alarmTimeDiff;
      nearestAlarmIndex = i;
    }
  }

  if (nearestAlarmIndex >= 0 && nearestAlarmIndex < numAlarms) {
    unsigned long timeTillBell = nearestTimeDiff / 60;  // Convert seconds to minutes
    displayTimeTillBell(timeTillBell);
  }
}

void displayTimeTillBell(unsigned long timeTillBell) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Next Bell in");
  lcd.setCursor(0, 1);
  lcd.print(timeTillBell);
  lcd.print(" minutes");
}

String formatDigits(int num) {
  if (num < 10) {
    return String("0") + String(num);
  }
  return String(num);
}

void saveSettings() {
  configFile = SD.open(filename, FILE_WRITE);

  if (configFile) {
    StaticJsonDocument<256> doc;
    JsonArray scheduleArray = doc.createNestedArray("schedule");

    for (int i = 0; i < numAlarms; i++) {
      JsonObject alarm = scheduleArray.createNestedObject();
      alarm["hour"] = h[i];
      alarm["minute"] = m[i];
    }

    serializeJson(doc, configFile);
    configFile.close();
  }
}

void loadBellSchedule() {
  if (SD.exists(filename)) {
    configFile = SD.open(filename);

    if (configFile) {
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, configFile);

      if (error) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Error loading bell");
        lcd.setCursor(0, 1);
        lcd.print("schedule");
        while (true) {
          blinkStatusLED(500);
        }
      }

      JsonArray scheduleArray = doc["schedule"];

      numAlarms = min((int)scheduleArray.size(), 16);  // Limit the number of alarms to 16

      for (int i = 0; i < numAlarms; i++) {
        JsonObject alarm = scheduleArray[i];
        h[i] = alarm["hour"].as<int>();
        m[i] = alarm["minute"].as<int>();
      }

      configFile.close();
    }
  }
}

void blinkStatusLED(unsigned long interval) {
  static unsigned long previousMillis = 0;
  static bool statusLEDState = false;

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    statusLEDState = !statusLEDState;
    digitalWrite(statusLED, statusLEDState ? HIGH : LOW);
  }
}