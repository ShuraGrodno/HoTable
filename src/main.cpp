#include <Arduino.h>

#define zeroPin 2
#define restRoomPin 4
#define bathRoomPin 7
#define dimerPin 6

volatile unsigned long zeroTime = 0;
volatile bool zeroTrigger = false;
unsigned long variablRezistor = 0;
unsigned long impulsDelay = 0;

void zeroTriggerISR() {
  zeroTime = micros();
  zeroTrigger = true;
}

void setup() {
  pinMode(zeroPin, INPUT);
  pinMode(restRoomPin, INPUT);
  pinMode(bathRoomPin, INPUT);
  pinMode(dimerPin, OUTPUT);
  attachInterrupt(0, zeroTriggerISR, RISING); //LOW,CHANGE,RISING,FALLING
}

void chatterContact() {
}

void loop() {


  if (zeroTrigger && (micros() - zeroTime) > variablRezistor) {
    digitalWrite(dimerPin, HIGH);
    impulsDelay = micros();
    zeroTrigger = false;
  }
  else if ((micros() - impulsDelay) > 350) {
    digitalWrite(dimerPin, LOW);
  }
}

