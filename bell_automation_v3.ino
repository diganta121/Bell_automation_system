#include <Wire.h>
#include "RTClib.h"

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
const char* filename = "bell_schedule.json";  // JSON file name

File configFile;

const int statusLED = 13;      // Status LED pin
const int workingLED = 12;     // Working LED pin
const int heartbeatLED = 14;   // Heartbeat LED pin

const unsigned long checkInterval = 20 * 60 * 1000;  // Check nearest bell every 20 minutes
const unsigned long halfInterval = checkInterval / 2; // Time to display bell information when halfway to the bell

unsigned long lastCheckTime = 0;
unsigned long lastDisplayTime = 0;

bool heartbeatLEDState = false;

bool powerSavingMode = false;
bool setTimeMode = false;
int setTimeState = 0; // 0: Hour, 1: Minute

int H = 0;
int M = 0;
int S = 0;

unsigned long timeTillBell = 0;

boolean bell_status = true;
boolean Over_ride = true;

int currentAlarm = -1;

unsigned long previousMillis = 0;
unsigned long interval = 20 * 60 * 1000;  // Check alarm every 20 minutes

int h[16] = {0};
int m[16] = {0};
int numAlarms = 0;

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

  digitalWrite(statusLED, LOW);      // Turn on the status LED to indicate program running
  digitalWrite(workingLED, LOW);     // Turn on the working LED
  digitalWrite(heartbeatLED, LOW);    // Turn off the heartbeat LED
  delay(500);

  digitalWrite(statusLED, HIGH);      // Turn on the status LED to indicate program running
  digitalWrite(workingLED, HIGH);     // Turn on the working LED
  digitalWrite(heartbeatLED, HIGH);    // Turn off the heartbeat LED
  delay(500);

  digitalWrite(statusLED, HIGH);      // Turn on the status LED to indicate program running
  digitalWrite(workingLED, HIGH);     // Turn on the working LED
  digitalWrite(heartbeatLED, LOW);    // Turn off the heartbeat LED
  delay(500);

  setTimeFromRTC();
}

void loop() {
  if (setTimeMode) {
    setTimeFromButtons();
    return;
  }

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
  }
  else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RTC stopped!!!");
    lcd.setCursor(0, 1);
    lcd.print("Press 'Select' to set time");
    blinkStatusLED(500);  // Blink status LED at 500ms intervals
    setTimeMode = true;
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
    lcd.print("Press 'Cancel' to quit");
    if (digitalRead(over_ride_off) == LOW) {
      Over_ride = false;
    }
  }
  digitalWrite(bell, LOW);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Bell is Enabled");
  lcd.setCursor(0, 1);
  lcd.print("Cancel button pressed");
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
  lcd.print("Press 'Cancel' to quit");
  unsigned long currentMillis = millis();

  // Check for exit button press to exit power saving mode
  while (true) {
    if (digitalRead(over_ride_off) == LOW) {
      powerSavingMode = false;
      break;
    }

    if (currentMillis - lastDisplayTime >= 1200000 ) {
      lastDisplayTime = currentMillis;
      updateLCD();
      checkNearestBell();
      currentMillis = millis();
    }

    blinkHeartbeatLED(5000);
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Bell is Enabled");
  lcd.setCursor(0, 1);
  lcd.print("Cancel button pressed");
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
    timeTillBell = nearestTimeDiff / 60;  // Convert seconds to minutes
    displayTimeTillBell(timeTillBell);
  }
  if (timeTillBell < 1260000){
    powerSavingMode = false;
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

void setTimeFromRTC() {
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Set the RTC to the time the sketch was compiled
}

void setTimeFromButtons() {
  const int btnUp = 18;   // Button to increment hour
  const int btnDown = 19; // Button to decrement hour
  const int btnSelect = 21; // Button to switch between setting hours and minutes
  const int btnCancel = 22; // Button to cancel setting time and resume normal operation

  if (setTimeState == 0) { // Setting Hour
    lcd.setCursor(0, 0);
    lcd.print("Set Time: ");
    lcd.print(formatDigits(H));
    lcd.print(":");
    lcd.print(formatDigits(M));
  } else { // Setting Minute
    lcd.setCursor(0, 0);
    lcd.print("Set Time: ");
    lcd.print(formatDigits(H));
    lcd.print(":");
    lcd.print(formatDigits(M));
  }

  if (digitalRead(btnUp) == LOW) {
    if (setTimeState == 0) {
      H = (H + 1) % 24;
    } else {
      M = (M + 1) % 60;
    }
    delay(250); // Debounce
  }

  if (digitalRead(btnDown) == LOW) {
    if (setTimeState == 0) {
      H = (H + 23) % 24;
    } else {
      M = (M + 59) % 60;
    }
    delay(250); // Debounce
  }

  if (digitalRead(btnSelect) == LOW) {
    setTimeState = (setTimeState + 1) % 2; // Toggle between setting hours and minutes
    delay(250); // Debounce
  }

  if (digitalRead(btnCancel) == LOW) {
    lcd.clear();
    setTimeMode = false;
    setTimeFromRTC(); // Reset RTC to the time the sketch was compiled
    return;
  }

  // Show blinking cursor for current setting (hours or minutes)
  if (millis() % 1000 < 500) {
    lcd.setCursor(setTimeState == 0 ? 10 : 13, 0);
    lcd.print("_");
  } else {
    lcd.setCursor(setTimeState == 0 ? 10 : 13, 0);
    lcd.print(" ");
  }

  // Save the new time when the 'Select' button is pressed and return to normal operation
  if (digitalRead(btnSelect) == LOW) {
    rtc.adjust(DateTime(rtc.now().year(), rtc.now().month(), rtc.now().day(), H, M, 0));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Time set to:");
    lcd.setCursor(0, 1);
    lcd.print(formatDigits(H));
    lcd.print(":");
    lcd.print(formatDigits(M));
    delay(2000);
    lcd.clear();
    setTimeMode = false;
    return;
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

void blinkHeartbeatLED(unsigned long interval) {
  static unsigned long previousMillis = 0;
  static bool heartbeatLEDState = false;

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    heartbeatLEDState = !heartbeatLEDState;
    digitalWrite(heartbeatLED, heartbeatLEDState ? HIGH : LOW);
  }

}
