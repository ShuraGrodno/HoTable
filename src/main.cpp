#include <Arduino.h>

#define zeroPin 2
#define restRoomPin 4
#define bathRoomPin 7
#define dimerPin 6

volatile unsigned long zeroTime = 0;
volatile bool zeroTrigger = false;
unsigned long variablRezistor = 0;
unsigned long impulsDelay = 0;
unsigned long starChatterTime = 0;
bool switchChatterDelay = false;
bool switchChatterEnable = false;

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
  bool switchRoom = digitalRead(restRoomPin) || digitalRead(bathRoomPin);
  if (switchRoom && !switchChatterDelay) {
    starChatterTime = millis();
    switchChatterDelay = true; 
  }
  else if ((millis() - starChatterTime) > 50 && switchChatterDelay) {
    switchChatterEnable = switchRoom;
  }
}
void loop() {
  chatterContact();

  if (zeroTrigger && (micros() - zeroTime) > variablRezistor) {
    digitalWrite(dimerPin, HIGH);
    impulsDelay = micros();
    zeroTrigger = false;
  }
  else if ((micros() - impulsDelay) > 350) {
    digitalWrite(dimerPin, LOW);
  }
}

