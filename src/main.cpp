#include <Arduino.h>

#define zeroPin 2                                         //
#define restRoomPin 4
#define bathRoomPin 7
#define dimerPin 6

const unsigned long DELAY_START_MOTOR = 10UL;

volatile unsigned long zeroTime = 0;                       //
volatile bool zeroTrigger = false;                         //
unsigned long variablRezistor = 0;                         //
unsigned long impulsDelay = 0;                             //
unsigned long startChatterTime = 0;                        // Переменная для фиксации времени нажатой кнопки чтобы сгладить дребезг
bool switchChatterDelay = false;                           // Резрешение на отсчет времени задершки чтобы пропустить дребезг контактов
bool switchChatterEnable = false;                          // Выходная переменная врлюченой клавиши после дребезга
unsigned long fanSpeed = 0;                                          //
bool oldStateSwitch = false;

//Режимы работы симистора для упровления вентилятором
enum FanMode {
  MAX,                                                      // Вентилятор на минимальных оборотах (Максисально тихо)
  NORMAL,                                                   // Средние обороты вентилятора
  MINI,                                                     // Максимальные обороты вентилятора
  NOLL                                                      // Вентилятор выключен
};

FanMode curentMode = NOLL;                                   // Выставляем первоначальное значение режима работы вентилятора


//Функция обработки прерывания
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
  }
}

//
void simistorControl() {

  if (switchChatterEnable) {// && (millis() - startChatterTime) > DELAY_START_MOTOR) {
    curentMode = MAX;
  }
  switch (curentMode) {
    case MAX:
      fanSpeed = 0;
    case NORMAL:
      fanSpeed = 5000;
    case MINI:
      fanSpeed = 8000;
    case NOLL:
      fanSpeed = 10000;
    default:
      break;
    }
  if (switchChatterEnable && zeroTrigger && (micros() - zeroTime) > 0) {
    digitalWrite(dimerPin, HIGH);
    impulsDelay = micros();
    zeroTrigger = false;
  }
  else if ((micros() - impulsDelay) > 350) {
    digitalWrite(dimerPin, LOW);
  }

}


void loop() {
  chatterContact();
  simistorControl();


}

