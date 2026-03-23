#include <Arduino.h>
#include <Timer.h>

#define zeroPin 2
#define restRoomPin 4
#define bathRoomPin 7
#define dimerPin 5

const unsigned long DELAY_START_MOTOR = 1000UL;     // * 1000 * 60;
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
  DelayOnFan,
  DelayOffFan,
  FanOn,
  FanOff
};

Mode FanMode = StandBy;
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
        FanMode = FanOn;
      }
      else {
        FanMode = DelayOnFan;
      }
    }
    else {
      FanMode = DelayOffFan;
    }
    //switchChatterEnable = switchRoom;//Принять на выход значение после таймера
    switchChatterDelay = false;//Сбросить таймер
  }
}

//
void FanControl() {
  switch (FanMode) {
    case StandBy:
      break;
    case DelayOnFan:
      DelayTimerFan = DELAY_START_MOTOR;
      break;
    case DelayOffFan:
      DelayTimerFan = DELAY_STOP_MOTOR;
      break;
    case FanOn:
      fanSpeed = 0UL;
      timerFan.reset();
    default:
      break;
  }
  if (timerFan.check(FanMode, DelayTimerFan)) {
    
    FanMode = StandBy;
  }
}  


//   //Ситуация когда требуется перейти из режима паниженых оборотов на максимальную мощность
//   if (switchChatterEnable && Fan) {
//     fanSpeed = 0UL;
//     delayStopFan = false;
//     timerSpeedChange.reset();//Сбросить таймер на пониженых оборотов
//   }
//   //Таймер на включение вентилятора
//   if (timerOnFan.check(switchChatterEnable && !Fan, DELAY_START_MOTOR)) {
//     onFan = !offFan;
//     fanSpeed = 0UL;
//   }
//   //Таймер на переход вентилятора в режим пониженых оборотов после выключения кнопки
//   if (timerSpeedChange.check(!switchChatterEnable && Fan, DELAY_DOWNTURN_MOTOR)) {
//     fanSpeed = 5000UL;
//     delayStopFan = true;
//   }
//   //Таймер на выключение вентилятора
//   if (timerOffFan.check(!switchChatterEnable && delayStopFan, DELAY_STOP_MOTOR)) {
//     delayStopFan = false; 
//     onFan = false;
//   }
//   //Таймер на максимальное время работы вентилятора
//   if (timerMaxWorkFan.check(Fan && !offFan, MAX_TIME_WORK_FAN)) {
//     offFan = true;
//   }
//   else if (!switchChatterEnable) {//Сброс таймера после выключения кнопки
//     offFan = false;
//   }
//   //Переменная запускающая работу сисистора
//   Fan = onFan && !offFan;
// }
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
  FanControl();
  simistorControl();
  // Serial.println(Fan);
}

