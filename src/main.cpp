#include <Arduino.h>
#include <Timer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <esp_sleep.h>
#include <NTPClient.h>

#define zeroPin 4
#define restRoomPin 25
#define bathRoomPin 26
#define dimerPin 27

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3*3600, 60000);

const char* ssid = "Xiaomi_040E";
const char* password = "H8#fqL2@";

const unsigned long DELAY_START_MOTOR = 1000UL;     // * 1000 * 60;
const unsigned long DELAY_RESTART_MOTOR = 100UL;
const unsigned long DELAY_DOWNTURN_MOTOR = 1000UL;  // * 1000 * 60;
const unsigned long DELAY_STOP_MOTOR = 1000UL;      // * 1000 * 60;
const unsigned long MAX_TIME_WORK_FAN = 5000UL;     // * 1000 * 60;
const unsigned long MAX_TIME_PAUZA_FAN = 3000UL; 
const unsigned long DELAY_SWITCH_CHATTER = 10UL;
const unsigned long DELAY_STENDBY_SLEEP = 5000UL;

volatile unsigned long zeroTime = 0;
volatile bool zeroTriggerOn = false;
bool zeroTriggerOff = false;
unsigned long impulsDelay = 0;

bool switchRoom = false;
bool oldStateSwitch = false;
uint64_t bitMask = (1ULL << restRoomPin) | (1ULL << bathRoomPin);
int setAutoWakeupTimer = 0;
bool blockSetAutoWakeupTimer = false;
bool nightMode = false;

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

//Функция обработки прерывания
void zeroTriggerISR() {
  zeroTime = micros();
  zeroTriggerOn = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(zeroPin, INPUT);
  pinMode(restRoomPin, INPUT);
  pinMode(bathRoomPin, INPUT);
  pinMode(dimerPin, OUTPUT);
  
  //Обработка прерываний
  attachInterrupt(digitalPinToInterrupt(zeroPin), zeroTriggerISR, FALLING); //LOW,CHANGE,RISING,FALLING
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  
  // Настройка OTA
  // ArduinoOTA.onStart([]() {
  //   String type;
  //   if (ArduinoOTA.getCommand() == U_FLASH)
  //   type = "sketch";
  //   else // U_SPIFFS
  //   type = "filesystem";
  //   Serial.println("Start updating " + type);
  // });
  // ArduinoOTA.onEnd([]() {
  //   Serial.println("\nEnd");
  // });
  // ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  //   Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  // });
  // ArduinoOTA.onError([](ota_error_t error) {
  //   Serial.printf("Error[%u]: ", error);
  //   if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
  //   else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
  //   else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
  //   else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
  //   else if (error == OTA_END_ERROR) Serial.println("End Failed");
  // });
  
  // ArduinoOTA.setPassword("admin");  // пароль для защиты
  // ArduinoOTA.begin();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  timeClient.begin();
  timeClient.update();
  //Настройка таймера пробуждения
  esp_sleep_enable_timer_wakeup((60 - timeClient.getMinutes()) * 1000000);
  Serial.println(60 - timeClient.getMinutes());
  //Настройка GPIO контактов на пробуждение
  esp_sleep_enable_ext1_wakeup(bitMask, ESP_EXT1_WAKEUP_ANY_HIGH);
}
//
bool disableNightTime() {
  while (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }
  timeClient.update();
  return (timeClient.getHours() > 6 && timeClient.getHours() < 22);
}
//Функция сглаживающая дребезг контактов включателей ванной комнаты и туалета
void chatterContact() {
  //Активировать Переменную от любой включеной кнопки
  switchRoom = disableNightTime() && (digitalRead(restRoomPin) || digitalRead(bathRoomPin));
  //Запустить таймен задержки дребезга
  if (timerSwitchChatter.check(switchRoom != oldStateSwitch, DELAY_SWITCH_CHATTER)) {
    if (switchRoom) {
      if (Fan) {
        TimerMode = OnFan;
        blokFan = false;
      }
      else {
        TimerMode = DelayOnFan;
      }
    }
    else if (Fan) {
      TimerMode = DelaySpeedChange;
    }
    else {
      TimerMode = StandBy;
      blokFan = false;
    }
    oldStateSwitch = switchRoom;
  }
}

//
void fanControl(int Mode) {
  //
  switch (Mode) {
    case 1:
      if (timerMaxWorkFan.check(Fan && !blokFan, MAX_TIME_WORK_FAN)) {
        blokFan = true;
        TimerMode = DelaySpeedChange;
      }
      break;
    case 2:
      if (timerMaxWorkFan.check(Fan && !blokFan, MAX_TIME_WORK_FAN)) {
        blokFan = true;
        Fan = false;
      }
      if (timerPauzaFan.check(blokFan, MAX_TIME_PAUZA_FAN)) {
        blokFan = false;
        Fan = true;
      }
      break;
  }
  //
  switch (TimerMode) {
    case StandBy:
      if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        while (WiFi.status() != WL_CONNECTED) {
          WiFi.reconnect();
        }
        timeClient.update();
        if (!timeClient.getMinutes()) {
          esp_sleep_enable_timer_wakeup(60 * 10000000);
          Serial.println("Коректировка таймера автоматическаго пробуждения произведена");
        }
        else {
          esp_sleep_enable_timer_wakeup((60 - timeClient.getMinutes()) * 1000000);
          Serial.println(60 - timeClient.getMinutes());
        }
      }
      if (timerStandBySleep.check(!switchRoom, DELAY_STENDBY_SLEEP)) {
        Serial.println("Перевести микроконтроллер в спящий режим");
        Serial.flush();
        esp_light_sleep_start();
      }
      break;
    case OnFan:
      DelayTimerFan = DELAY_RESTART_MOTOR;
      break;
    case DelayOnFan:
      DelayTimerFan = DELAY_START_MOTOR;
      break;
    case DelaySpeedChange:
      DelayTimerFan = DELAY_DOWNTURN_MOTOR;
      break;
    case DelayOffFan:
      DelayTimerFan = DELAY_STOP_MOTOR;
      break;
  }
  //
  if (timerFan.check(TimerMode, DelayTimerFan)) {
    //
    switch (TimerMode) {
      case StandBy:
        break;
      case OnFan:
        fanSpeed = 0UL;
        TimerMode = StandBy;
        Serial.println("Включить вентилятор на максимальные обороты");
        break;
      case DelayOnFan:
        Fan = true;
        fanSpeed = 0UL;
        TimerMode = StandBy;
        Serial.println("Включить вентилятор после отработки таймера");
        break;
      case DelaySpeedChange:
        fanSpeed = 5000UL;
        TimerMode = DelayOffFan;
        timerFan.reset();
        Serial.println("Перевести вентилятор в режим пониженых оборотов");
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

void simistorControl() {
  //Регулятор режима работы симистора
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


void loop() {
  // ArduinoOTA.handle();
  chatterContact();
  fanControl(1);
  simistorControl();
}

