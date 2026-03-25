#include <Arduino.h>
#include <Timer.h>

#define zeroPin 2
#define restRoomPin 4
#define bathRoomPin 7
#define dimerPin 5

const unsigned long DELAY_START_MOTOR = 1000UL;     // * 1000 * 60;
const unsigned long DELAY_RESTART_MOTOR = 100UL;
const unsigned long DELAY_DOWNTURN_MOTOR = 1000UL;  // * 1000 * 60;
const unsigned long DELAY_STOP_MOTOR = 1000UL;      // * 1000 * 60;
const unsigned long MAX_TIME_WORK_FAN = 1000UL;     // * 1000 * 60;
const unsigned long DELAY_SWITCH_CHATTER = 10UL;

volatile unsigned long zeroTime = 0;
volatile bool zeroTriggerOn = false;
bool zeroTriggerOff = false;
unsigned long impulsDelay = 0;

bool switchChatterEnable = false;
bool switchChatterDelay = false;
bool oldStateSwitch = false;

enum Mode {
  StandBy,
  OnFan,
  DelayOnFan,
  DelaySpeedChange,
  DelayOffFan
};

Mode TimerMode = StandBy;
bool EnableTimerFan = false;
unsigned long DelayTimerFan = 0UL;
bool Fan = false;
// bool onFan = false;
// bool offFan = false;
// bool delayStopFan = false;
// bool maxWorkFan = false;
unsigned long fanSpeed = 0UL;

Timer timerOnFan;
Timer timerSpeedChange;
Timer timerOffFan;
Timer timerMaxWorkFan;
Timer timerSwitchChatter;

Timer timerFan;

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
  attachInterrupt(0, zeroTriggerISR, FALLING); //LOW,CHANGE,RISING,FALLING
}

//Функция сглаживающая дребезг контактов включателей ванной комнаты и туалета
void chatterContact() {
  //Активировать Переменную от любой включеной кнопки
  bool switchRoom = digitalRead(restRoomPin) || digitalRead(bathRoomPin);
  //Опрос состояния контактов
  if (switchRoom != oldStateSwitch) {
    oldStateSwitch = switchRoom;
    switchChatterDelay = true;//Активировать таймер
  }
  //Запустить таймен задержки дребезга
  if (timerSwitchChatter.check(switchChatterDelay, DELAY_SWITCH_CHATTER)) {
    if (switchRoom) {
      if (Fan) {
        TimerMode = OnFan;
      }
      else {
        TimerMode = DelayOnFan;
      }
    }
    else {
      TimerMode = DelaySpeedChange;
    }
    switchChatterDelay = false;//Сбросить таймер
  }
}

//
void fanControl() {

  switch (TimerMode) {
    case StandBy:
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
  if (timerFan.check(TimerMode, DelayTimerFan)) {
    switch (TimerMode) {
      case StandBy:
        break;
      case OnFan:
        fanSpeed = 0UL;
        TimerMode = StandBy;
        break;
      case DelayOnFan:
        Fan = true;
        fanSpeed = 0UL;
        TimerMode = StandBy;
        break;
      case DelaySpeedChange:
        fanSpeed = 5000UL;
        TimerMode = DelayOffFan;
        timerFan.reset();
        break;
      case DelayOffFan:
        Fan = false;
        TimerMode = StandBy;
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
  chatterContact();
  fanControl();
  simistorControl();
  // Serial.println(TimerMode);
}

