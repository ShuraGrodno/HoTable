#include <Arduino.h>
// ----- Библиотека таймер --------
#include <Timer.h>
// ----- Библиотеки для подключению к WiFi ------------
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
// ----- Библиотека для просмотра Serial Monitor через WiFi ------
#include <TelnetStream.h>
// ----- Библиотека для загрузки прошивок по безпроводному протоколу -------
#include <ArduinoOTA.h>
// ----- Библиотека для загрузки с портала "pool.ntp.org" времени -----
#include <NTPClient.h>
// ----- Библиотека для OLED 0.96" (U8g2) -----
#include <U8g2lib.h>
#include <Wire.h>

// ----- Настройка пинов для ESP8266 (NodeMCU v2/v3) -----
#define zeroPin        5       // D1  (GPIO05) - детектор нуля (прерывание)
#define restRoomPin    15      // D8  (GPIO15) - кнопка туалета
#define bathRoomPin    13      // D7  (GPIO13) - кнопка ванной
#define dimerPin       4       // D2  (GPIO04) - управление симистором (только выход)

// ---------------- OLED ----------------
unsigned long lastDisplay                  = 0;

// ----- I2C для OLED (обычно SDA=GPI12, SCL=GPI14) -----
#define OLED_SDA       12       // D6
#define OLED_SCL       14       // D5
U8G2_SSD1306_128X64_NONAME_F_SW_I2C oled(U8G2_R0, /* clock=*/ OLED_SCL, /* data=*/ OLED_SDA, /* reset=*/ U8X8_PIN_NONE);

// ----- Остальные константы -----
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3*3600, 60000);

// ----- Данные точки доступа WiFi --------
const char* ssid                          = "Xiaomi_040E";
const char* password                      = "H8#fqL2@";

const unsigned long DELAY_START_MOTOR     = 1000UL;
const unsigned long DELAY_RESTART_MOTOR   = 100UL;
const unsigned long DELAY_DOWNTURN_MOTOR  = 2000UL;
const unsigned long DELAY_STOP_MOTOR      = 5000UL;
const unsigned long MAX_TIME_WORK_FAN     = 10000UL;
const unsigned long MAX_TIME_PAUZA_FAN    = 3000UL;
const unsigned long DELAY_SWITCH_CHATTER  = 10UL;

volatile unsigned long zeroTime           = 0;
volatile bool zeroTriggerOn               = false;
bool zeroTriggerOff                       = false;
unsigned long impulsDelay                 = 0;

bool switchRoom                           = false;
bool oldStateSwitch                       = false;

enum Mode {
  StandBy,
  OnFan,
  DelayOnFan,
  DelaySpeedChange,
  DelayOffFan
};

Mode TimerMode                            = StandBy;
unsigned long DelayTimerFan               = 0UL;
bool Fan                                  = false;
bool blokFan                              = false;
unsigned long fanSpeed                    = 10000UL;

// ---------------- ПЛАВНОСТЬ ----------------
unsigned long lastRamp                    = 0UL;
unsigned long targetSpeed                 = 0UL;


Timer timerFan;
Timer timerMaxWorkFan;
Timer timerPauzaFan;
Timer timerSwitchChatter;

// ----- Функция разрешения работы по времени суток -----
bool disableNightTime() {
  timeClient.update();
  int hour = timeClient.getHours();
  return (hour > 6 && hour < 22);
}

// ----- Вывод информации на OLED -----
void updateOLED() {
  oled.firstPage();
  do{
    oled.setCursor(0, 10);
    timeClient.update();
    oled.print(timeClient.getFormattedTime());
    
    oled.setCursor(0, 25);
    oled.print("Night: ");
    oled.print(disableNightTime() ? "OFF" : "ON");
    
    oled.setCursor(0, 40);
    oled.print("IP: ");
    oled.print(WiFi.localIP());
  } while(oled.nextPage());
}

// ----- Прерывание детектора нуля -----
void IRAM_ATTR zeroTriggerISR() {
  zeroTime = micros();
  zeroTriggerOn = true;
}

// ================= Плавный набор и снижение оборотов двигателя =================
void updateSpeedSmooth() {
  if (millis() - lastRamp < 5) return;
  lastRamp = millis();

  if (fanSpeed < targetSpeed) fanSpeed += 50;
  else if (fanSpeed > targetSpeed) fanSpeed -= 50;
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
void fanControl(bool Mode) {
  switch (Mode) {
    case false:
      if (timerMaxWorkFan.check(Fan && !blokFan, MAX_TIME_WORK_FAN)) {
        Serial.println("Достигнут максимальное время работы вентилятора");
        blokFan = true;
        TimerMode = DelaySpeedChange;
      }
      break;
    case true:
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
        targetSpeed = 0UL;
        TimerMode = StandBy;
        Serial.println("Включить вентилятор на максимальные обороты");
        break;
      case DelayOnFan:
        Fan = true;
        targetSpeed = 0UL;
        TimerMode = StandBy;
        Serial.println("Включить вентилятор после таймера");
        break;
      case DelaySpeedChange:
        targetSpeed = 5000UL;
        TimerMode = DelayOffFan;
        timerFan.reset();
        Serial.println("Перевести на пониженные обороты");
        break;
      case DelayOffFan:
        targetSpeed = 10000UL;
        if (fanSpeed == targetSpeed) {
          Fan = false;
          blokFan = false;
          TimerMode = StandBy;
          Serial.println("Выключить вентилятор");
        }
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
  if (zeroTriggerOff && (micros() - impulsDelay) > 300) {
    digitalWrite(dimerPin, LOW);
    zeroTriggerOff = false;
  }
}

// ----- Setup -----
void setup() {
  Serial.begin(115200);
  pinMode(zeroPin, INPUT);
  pinMode(restRoomPin, INPUT);
  pinMode(bathRoomPin, INPUT);
  pinMode(dimerPin, OUTPUT);
  digitalWrite(dimerPin, LOW);

  attachInterrupt(digitalPinToInterrupt(zeroPin), zeroTriggerISR, FALLING);  //LOW,CHANGE,RISING,FALLING

  // ---- I2C для OLED ----
  Wire.begin(OLED_SDA, OLED_SCL);
  oled.begin();
  oled.setPowerSave(0);
  oled.setFont(u8g2_font_ncenB08_tr);
  
  oled.firstPage();
  do{
    oled.setCursor(0, 10);
    oled.print("Loading...");
  } while(oled.nextPage());

  // ---- WiFi ----
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
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
  // ----- NTPClient ------
  timeClient.begin();
  // ----- Обновить экран ------
  updateOLED();
}

// ----- Loop -----
void loop() {
  ArduinoOTA.handle();
  if (TelnetStream.available()) {
    TelnetStream.read();
  }
  chatterContact();
  fanControl(false);
  updateSpeedSmooth();
  simistorControl();
  if (!Fan && (millis() - lastDisplay > 100)) {
    lastDisplay = millis();
    updateOLED();
  }
  yield();
}
