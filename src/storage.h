#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>   // полный тип AsyncWebSocket (нужен для ws.textAll)

// Параметры, которые живут в main.cpp (сюда только объявления-ссылки)
extern int   currentAngle;   // угол поворота, град
extern float currentUstep;   // величина микрошага, град
extern bool  microMode;      // режим микрошага вкл/выкл
extern float scanSpeed;      // скорость сканирования, град/сек
extern float scanAngle;      // угол сканирования, град
extern AsyncWebSocket ws;    // отправка сообщений странице

void saveBaseParams();                   // записать текущие значения в NVS
void loadBaseParams(bool toSerialOnly);  // прочитать из NVS, применить и отправить странице

#endif // STORAGE_H
