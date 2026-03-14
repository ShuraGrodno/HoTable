#include <Arduino.h>
#include <D:\Developer\ArduinoProject\HoTable\lib\Timer.h>

#define zeroPin 2
#define restRoomPin 4
#define bathRoomPin 7
#define dimerPin 5

const unsigned long DELAY_START_MOTOR = 1000UL;
const unsigned long DELAY_DOWNTURN_MOTOR = 1000UL;
const unsigned long DELAY_STOP_MOTOR = 1000UL;
const unsigned long DELAY_SWITCH_CHATTER = 10UL;

volatile unsigned long zeroTime = 0;
volatile bool zeroTriggerOn = false;
bool zeroTriggerOff = false;
unsigned long impulsDelay = 0;

bool switchChatterEnable = false;
bool switchChatterDelay = false;
bool oldStateSwitch = false;

bool Fan = false;
bool delayStopFan = false;
unsigned long fanSpeed = 0UL;

Timer timerOnFan;
Timer timerSpeedChange;
Timer timerOffFan;
Timer timerSwitchChatter;

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
    oldStateSwitch = switchRoom;
    switchChatterDelay = true;
  }
  if (timerSwitchChatter.check(switchChatterDelay, DELAY_SWITCH_CHATTER)) {
    switchChatterEnable = switchRoom;
    switchChatterDelay = false;
  }
}

//
void simistorControl() {
  if (switchChatterEnable && Fan) {
    fanSpeed = 0UL;
    delayStopFan = false;
    timerSpeedChange.reset();
  }
  if (timerOnFan.check(switchChatterEnable && !Fan, DELAY_START_MOTOR)) {
    Fan = true;
    fanSpeed = 0UL;
  }
  if (timerSpeedChange.check(!switchChatterEnable && Fan, DELAY_DOWNTURN_MOTOR)) {
    fanSpeed = 5000UL;
    delayStopFan = true;
  }
  if (timerOffFan.check(!switchChatterEnable && delayStopFan, DELAY_STOP_MOTOR)) {
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

