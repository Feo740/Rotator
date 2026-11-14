#ifndef MOTOR_H
#define MOTOR_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// --- Внешние переменные из main.cpp ---
extern bool scanning;
extern float scanSpeed;
extern AsyncWebSocket ws;

// --- Пины (ESP32 -> транзисторный ключ -> входы оптопар MKS TMC2160-OC) ---
#define PIN_STEP  25
#define PIN_DIR   26
#define PIN_EN    27   // активен НИЗКИМ уровнем: LOW = драйвер включён

#define LEDC_CH   0
#define LEDC_RES  10   // разрешение 10 бит -> заполнение 512 = 50%

// --- Кинематика ---
// Микрошаг драйвера 1/64 (DIP на плате): 200 шагов/об * 64 = 12800 мкрошагов/об
const float USTEP_PER_DEG = 12800.0f / 360.0f;  // ~35.56 мкрошага на градус
const int   ROTATE_FREQ   = 4000;               // частота STEP для ручного поворота, Гц

// --- Защита кабеля датчика: жёсткие пределы поворота ---
const float POS_MIN  = 0.0f;    // нижний предел, град
const float POS_MAX  = 360.0f;  // верхний предел, град
const float POS_WARN = 15.0f;   // предупреждение, когда до границы осталось меньше

// --- Состояние мотора ---
float posDeg      = 0.0;    // текущее положение вала, град (0 при включении)
float fracSteps   = 0.0;    // накопитель дробных микрошагов (для ustep 0.5 град)
long  stepsToGo   = 0;      // остаток импульсов текущего ручного движения
bool  dirCW       = true;   // текущее направление
bool  moving      = false;  // идёт ручной поворот
float rotAngle    = 0.0;    // фактический угол текущего ручного движения
uint32_t rotStartMs = 0;

// --- Обёртки над LEDC: совместимы с ядром 2.x и 3.x ---
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void pulseSetup(float freq) { ledcChangeFrequency(PIN_STEP, (uint32_t)freq, LEDC_RES); }
void pulseStart(float freq) { pulseSetup(freq); ledcWrite(PIN_STEP, (1 << LEDC_RES) / 2); }
void pulseStop()            { ledcWrite(PIN_STEP, 0); }
void pulseInit()            { ledcAttach(PIN_STEP, 1000, LEDC_RES); pulseStop(); }
#else
void pulseSetup(float freq) { ledcSetup(LEDC_CH, freq, LEDC_RES); }
void pulseStart(float freq) { pulseSetup(freq); ledcWrite(LEDC_CH, (1 << LEDC_RES) / 2); }
void pulseStop()            { ledcWrite(LEDC_CH, 0); }
void pulseInit()            { ledcSetup(LEDC_CH, 1000, LEDC_RES); ledcAttachPin(PIN_STEP, LEDC_CH); pulseStop(); }
#endif

void motorInit() {
  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_EN,  OUTPUT);
  digitalWrite(PIN_EN, HIGH);   // драйвер выключен до старта
  pulseInit();
}

void motorEnable(bool on) {
  digitalWrite(PIN_EN, on ? LOW : HIGH);  // EN активен нулём
}

void motorSetDir(bool cw) {
  dirCW = cw;
  digitalWrite(PIN_DIR, cw ? HIGH : LOW);
}

// Скорость в град/сек -> частота STEP в Гц
float degPerSecToFreq(float v) {
  return v * USTEP_PER_DEG;
}

void stopMotor() {
  if (moving) {
    // доначисляем фактический пройденный угол в позицию
    posDeg += (dirCW ? 1.0f : -1.0f) * (float)stepsToGo / USTEP_PER_DEG;
  }
  moving = false;
  stepsToGo = 0;
  pulseStop();
  Serial.println("[MOTOR] Stop");
}

// Ручной поворот на угол с учётом дробного микрошага и пределов 0..360
void rotate(float angle, bool cw) {
  if (scanning) {
    scanning = false;
    Serial.println("[SCAN] выкл. — ручное управление");
    ws.textAll("{\"cmd\":\"scanState\",\"val\":0}");
  }
  if (moving) stopMotor();

  // --- Защита кабеля: урезаем/блокируем по пределам 0..360 ---
  float clamped = angle;
  float target  = posDeg + (cw ? clamped : -clamped);
  if (target > POS_MAX) {
    clamped = POS_MAX - posDeg;
    if (clamped < 0.01f) {
      Serial.println("[SAFE] Граница 360 град — поворот заблокирован");
      return;
    }
    Serial.printf("[SAFE] Угол урезан: %.1f -> %.1f град (предел %.0f)\n",
                  angle, clamped, POS_MAX);
  } else if (target < POS_MIN) {
    clamped = posDeg - POS_MIN;
    if (clamped < 0.01f) {
      Serial.println("[SAFE] Граница 0 град — поворот заблокирован");
      return;
    }
    Serial.printf("[SAFE] Угол урезан: %.1f -> %.1f град (предел %.0f)\n",
                  angle, clamped, POS_MIN);
  }
  angle = clamped;

  // --- Предупреждение у границы ---
  target = posDeg + (cw ? angle : -angle);
  if (target <= POS_MIN + POS_WARN || target >= POS_MAX - POS_WARN) {
    Serial.printf("[SAFE] Внимание: после поворота позиция %.1f град — близко к границе\n", target);
  }

  motorSetDir(cw);

  float want = angle * USTEP_PER_DEG + fracSteps;
  long  whole = (long)want;
  fracSteps = want - whole;              // дробь копим до следующего поворота
  stepsToGo = whole;

  if (stepsToGo > 0) {
    rotAngle = (float)stepsToGo / USTEP_PER_DEG;
    moving = true;
    rotStartMs = millis();
    pulseStart(ROTATE_FREQ);
    Serial.printf("[MOTOR] Поворот %.1f град (%ld мкрошагов) %s\n",
                  rotAngle, stepsToGo, cw ? "CW" : "CCW");
  } else {
    Serial.println("[MOTOR] Поворот меньше шага — отложен в накопитель");
  }
}

// Разворот при сканировании: пауза импульсов, смена DIR, продолжение
void scanReverse(bool cw) {
  pulseStop();
  delayMicroseconds(100);                // время на смену направления
  motorSetDir(cw);
  pulseStart(degPerSecToFreq(scanSpeed));
}

// Смена скорости сканирования на лету
void scanApplySpeed() {
  if (scanning) pulseSetup(degPerSecToFreq(scanSpeed));
}

#endif // MOTOR_H
