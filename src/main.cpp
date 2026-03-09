#include <Arduino.h>
#include <D:\Developer\ArduinoProject\HoTable\lib\Timer.h>

#define zeroPin 2
#define restRoomPin 4
#define bathRoomPin 7
#define dimerPin 5

const unsigned long DELAY_START_MOTOR = 1000UL;
const unsigned long DELAY_DOWNTURN_MOTOR = 1000UL;
const unsigned long DELAY_STOP_MOTOR = 1000UL;

volatile unsigned long zeroTime = 0;
volatile bool zeroTriggerOn = false;
bool zeroTriggerOff = false;
unsigned long impulsDelay = 0;

bool switchChatterEnable = false;
bool switchChatterDelay = false;
bool oldStateSwitch = false;
unsigned long startChatterTime = 0;

bool Fan = false;
bool delayStopFan = false;
unsigned long timeOffFan = 0UL;
unsigned long fanSpeed = 0UL;
unsigned long timeOnLight = 0UL;
unsigned long timeOffLight = 0UL;

Timer Timer1;
Timer Timer2;
Timer Timer3;

// bool oldStateTimer;
// bool timerComplet;
// unsigned long startinPointTimer;

// bool Timer(bool enable, unsigned long time) {
//   if (enable != oldStateTimer) {
//     startinPointTimer = millis();
//     oldStateTimer = enable;
//     if (!enable) {
//       timerComplet = false;
//     }
//   }
//   if (enable && !timerComplet && (millis() - startinPointTimer) > time) {
//     timerComplet = true;
//   }
//   return timerComplet;
// }

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
  bool switchRoom = digitalRead(restRoomPin) || digitalRead(bathRoomPin);
  if (switchRoom != oldStateSwitch) {
    startChatterTime = millis();
    oldStateSwitch = switchRoom;
    switchChatterDelay = true;
  }
  if (switchChatterDelay && (millis() - startChatterTime) > 10) {
    switchChatterEnable = switchRoom;
    switchChatterDelay = false;
    if (switchChatterEnable) {
      timeOnLight = millis();
    }
    else {
      timeOffLight = millis();
    }
  }
}

//
void simistorControl() {
  if (Timer1.check(switchChatterEnable, DELAY_START_MOTOR)) {//(switchChatterEnable && (millis() - timeOnLight) > DELAY_START_MOTOR) {
    Fan = true;
    fanSpeed = 0UL;
  }
  if (Timer2.check(!switchChatterEnable && Fan, DELAY_DOWNTURN_MOTOR)) {//(!switchChatterEnable && Fan && (millis() - timeOffLight) > DELAY_DOWNTURN_MOTOR) {
    fanSpeed = 5000UL;
    delayStopFan = true;
  }
  if (Timer3.check(delayStopFan, DELAY_STOP_MOTOR)) {//(delayStopFan && (millis() - timeOffFan) > DELAY_STOP_MOTOR) {
    delayStopFan = false; 
    Fan = false;
  }


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
  simistorControl();
  // Serial.println(Fan);
}

