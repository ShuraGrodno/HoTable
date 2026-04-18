#include <Arduino.h>
#include <Timer.h>                // ваш собственный класс Timer (должен быть в папке проекта)
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif
#include <TelnetStream.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>

// ----- Библиотека для OLED 0.96" (U8g2) -----
#include <U8g2lib.h>
#include <Wire.h>

// ----- Настройка пинов для ESP8266 (NodeMCU v2/v3) -----
#define zeroPin        14      // D5  (GPIO14) - детектор нуля (прерывание)
#define restRoomPin    12      // D6  (GPIO12) - кнопка туалета
#define bathRoomPin    13      // D7  (GPIO13) - кнопка ванной
#define dimerPin       16      // D0  (GPIO16) - управление симистором (только выход)

// ----- I2C для OLED (обычно SDA=GPIO4, SCL=GPIO5) -----
#define OLED_SDA       4       // D2
#define OLED_SCL       5       // D1
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// ----- Остальные константы -----
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3*3600, 60000);

const char* ssid = "Xiaomi_040E";
const char* password = "H8#fqL2@";

const unsigned long DELAY_START_MOTOR     = 1000UL;
const unsigned long DELAY_RESTART_MOTOR   = 100UL;
const unsigned long DELAY_DOWNTURN_MOTOR  = 1000UL;
const unsigned long DELAY_STOP_MOTOR      = 1000UL;
const unsigned long MAX_TIME_WORK_FAN     = 5000UL;
const unsigned long MAX_TIME_PAUZA_FAN    = 3000UL;
const unsigned long DELAY_SWITCH_CHATTER  = 10UL;
const unsigned long DELAY_STENDBY_SLEEP   = 5000UL;

volatile unsigned long zeroTime = 0;
volatile bool zeroTriggerOn = false;
bool zeroTriggerOff = false;
unsigned long impulsDelay = 0;

bool switchRoom = false;
bool oldStateSwitch = false;

enum Mode {
  StandBy,
  OnFan,
  DelayOnFan,
  DelaySpeedChange,
  DelayOffFan
};

Mode TimerMode = StandBy;
unsigned long DelayTimerFan = 0UL;
bool Fan = false;
bool blokFan = false;
unsigned long fanSpeed = 0UL;

Timer timerFan;
Timer timerMaxWorkFan;
Timer timerPauzaFan;
Timer timerSwitchChatter;
Timer timerStandBySleep;

// ----- Прерывание детектора нуля -----
void IRAM_ATTR zeroTriggerISR() {
  zeroTime = micros();
  zeroTriggerOn = true;
}

// ----- Функция разрешения работы по времени суток -----
bool disableNightTime() {
  timeClient.update();
  int hour = timeClient.getHours();
  return (hour > 6 && hour < 22);
}

// ----- Устранение дребезга контактов -----
void chatterContact() {
  switchRoom = disableNightTime() && (digitalRead(restRoomPin) || digitalRead(bathRoomPin));
  if (timerSwitchChatter.check(switchRoom != oldStateSwitch, DELAY_SWITCH_CHATTER)) {
    if (switchRoom) {
      if (Fan) {
        TimerMode = OnFan;
        blokFan = false;
      } else {
        TimerMode = DelayOnFan;
      }
    } else if (Fan) {
      TimerMode = DelaySpeedChange;
    } else {
      TimerMode = StandBy;
      blokFan = false;
    }
    oldStateSwitch = switchRoom;
  }
}

// ----- Управление вентилятором (таймеры максимальной работы/паузы) -----
void fanControl(int Mode) {
  switch (Mode) {
    case 1:
      if (timerMaxWorkFan.check(Fan && !blokFan, MAX_TIME_WORK_FAN)) {
        Serial.println("Достигнут максимальное время работы вентилятора");
        blokFan = true;
        TimerMode = DelaySpeedChange;
      }
      break;
    case 2:
      if (timerMaxWorkFan.check(Fan && !blokFan, MAX_TIME_WORK_FAN)) {
        Serial.println("Достигнут максимум. Остановить вентилятор на паузу");
        blokFan = true;
        Fan = false;
      }
      if (timerPauzaFan.check(blokFan, MAX_TIME_PAUZA_FAN)) {
        Serial.println("Пауза окончена. Включить на максимум");
        blokFan = false;
        Fan = true;
      }
      break;
  }

  switch (TimerMode) {
    case StandBy:          break;
    case OnFan:            DelayTimerFan = DELAY_RESTART_MOTOR; break;
    case DelayOnFan:       DelayTimerFan = DELAY_START_MOTOR;   break;
    case DelaySpeedChange: DelayTimerFan = DELAY_DOWNTURN_MOTOR; break;
    case DelayOffFan:      DelayTimerFan = DELAY_STOP_MOTOR;    break;
  }

  if (timerFan.check(TimerMode != StandBy, DelayTimerFan)) {
    switch (TimerMode) {
      case StandBy: break;
      case OnFan:
        fanSpeed = 0UL;
        TimerMode = StandBy;
        Serial.println("Включить вентилятор на максимальные обороты");
        break;
      case DelayOnFan:
        Fan = true;
        fanSpeed = 0UL;
        TimerMode = StandBy;
        Serial.println("Включить вентилятор после таймера");
        break;
      case DelaySpeedChange:
        fanSpeed = 5000UL;
        TimerMode = DelayOffFan;
        timerFan.reset();
        Serial.println("Перевести на пониженные обороты");
        break;
      case DelayOffFan:
        Fan = false;
        blokFan = false;
        TimerMode = StandBy;
        Serial.println("Выключить вентилятор");
        break;
    }
  }
}

// ----- Управление симистором -----
void simistorControl() {
  if (Fan && zeroTriggerOn && (micros() - zeroTime) > fanSpeed) {
    digitalWrite(dimerPin, HIGH);
    impulsDelay = micros();
    zeroTriggerOn = false;
    zeroTriggerOff = true;
  }
  if (zeroTriggerOff && (micros() - impulsDelay) > 350) {
    digitalWrite(dimerPin, LOW);
    zeroTriggerOff = false;
  }
}

// ----- Вывод информации на OLED -----
void updateOLED() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < 200) return;  // обновляем 5 раз в секунду
  lastUpdate = millis();

  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.setCursor(0, 10);
  oled.print("Fan: ");
  oled.print(Fan ? "ON " : "OFF");
  oled.print("  Mode: ");
  switch (TimerMode) {
    case StandBy:           oled.print("STBY"); break;
    case OnFan:             oled.print("ON  "); break;
    case DelayOnFan:        oled.print("dON "); break;
    case DelaySpeedChange:  oled.print("dSPD"); break;
    case DelayOffFan:       oled.print("dOFF"); break;
  }

  oled.setCursor(0, 25);
  oled.print("Speed: ");
  oled.print(fanSpeed);
  oled.print(" us");

  oled.setCursor(0, 40);
  oled.print("IP: ");
  oled.print(WiFi.localIP());

  oled.setCursor(0, 55);
  timeClient.update();
  oled.print(timeClient.getFormattedTime());

  oled.sendBuffer();
}

// ----- Setup -----
void setup() {
  Serial.begin(115200);
  pinMode(zeroPin, INPUT);
  pinMode(restRoomPin, INPUT);
  pinMode(bathRoomPin, INPUT);
  pinMode(dimerPin, OUTPUT);
  digitalWrite(dimerPin, LOW);

  attachInterrupt(digitalPinToInterrupt(zeroPin), zeroTriggerISR, RISING);

  // ---- I2C для OLED ----
  Wire.begin(OLED_SDA, OLED_SCL);
  oled.begin();
  oled.setPowerSave(0);
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 10, "Connecting WiFi...");
  oled.sendBuffer();

  // ---- WiFi ----
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  WiFi.setSleepMode(WIFI_NONE_SLEEP);   // отключаем энергосбережение для стабильности OTA

  // ---- OTA ----
  ArduinoOTA.setHostname("esp8266_Bathroom");
  ArduinoOTA.setPassword("admin");
  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    TelnetStream.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    TelnetStream.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    TelnetStream.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    TelnetStream.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) TelnetStream.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) TelnetStream.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) TelnetStream.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) TelnetStream.println("Receive Failed");
    else if (error == OTA_END_ERROR) TelnetStream.println("End Failed");
  });
  ArduinoOTA.begin();

  // ---- TelnetStream ----
  TelnetStream.begin();
  TelnetStream.print("IP address: ");
  TelnetStream.println(WiFi.localIP());

  timeClient.begin();

  // ---- Вывод на OLED готовности ----
  oled.clearBuffer();
  oled.drawStr(0, 10, "WiFi OK");
  oled.drawStr(0, 25, WiFi.localIP().toString().c_str());
  oled.sendBuffer();
  delay(2000);
}

// ----- Loop -----
void loop() {
  ArduinoOTA.handle();
  if (TelnetStream.available()) {
    TelnetStream.read();   // поддержка Telnet-соединения
  }
  chatterContact();
  fanControl(1);           // режим 1 (простой таймер максимальной работы)
  simistorControl();
  updateOLED();
}