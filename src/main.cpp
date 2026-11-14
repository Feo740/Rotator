#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "web_page.h"      // <-- страница теперь здесь


// --- Настройки Wi‑Fi ---
const char* ssid = "Rotator";
const char* password = "12345678";

// --- Глобальные переменные ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");


bool holdMode = false;



void holdOff(bool byUser) {
  if (holdMode) {
    holdMode = false;
    Serial.println(byUser ? "[HOLD] выкл. удержание" : "[HOLD] удержание снято командой движения");
    ws.textAll("{\"cmd\":\"holdState\",\"val\":0}"); // снять фиксацию кнопки HOLD на странице
  }
}

// --- Состояние ---
float motorSpeed = 50.0;   // оставим на будущее, в град/сек
float lastAngle = 0.0;     // последний выполненный угол
bool  microMode = false;   // режим микрошага (для лога/будущей логики)
// --- Сканирование ---
bool   scanning    = false;
float  scanSpeed   = 10.0;   // град/сек
float  scanAngle   = 90.0;   // полный размах, град
float  scanPos     = 0.0;    // виртуальное положение, град (от -angle/2 до +angle/2)
int    scanDir     = 1;      // 1 = вперёд, -1 = назад
uint32_t lastScanMs = 0;

void scanStart() {
  scanning = true;
  scanPos = 0.0;
  scanDir = 1;
  Serial.printf("[SCAN] вкл. режим сканирования скорость %.0f град/сек, угол %.0f град\n",
                scanSpeed, scanAngle);
}

void scanStop() {
  scanning = false;
  Serial.println("[SCAN] выкл. режим сканирования");
}

void scanUpdateParams() {
  if (scanning) {
    Serial.printf("[SCAN] новые параметры: скорость %.0f град/сек, угол %.0f град\n",
                  scanSpeed, scanAngle);
    // если положение вышло за новый размах — вернём в пределы
    float half = scanAngle / 2.0;
    if (scanPos > half)  scanPos = half;
    if (scanPos < -half) scanPos = -half;
  }
}


void rotate(float angle, bool cw) {
  if (scanning) {
    scanning = false;
    Serial.println("[SCAN] выкл. — ручное управление");
    ws.textAll("{\"cmd\":\"scanState\",\"val\":0}");
  }
  Serial.printf("[MOTOR] Повернул на %.1f град. %s\n", angle, cw ? "CW" : "CCW");
}



// --- Заглушки для управления мотором (сюда позже вставишь TMC2160) ---
void moveMotor(int speed, int angle, bool cw) {
  // Здесь будет логика шаговика и TMC2160
  Serial.printf("[MOTOR] Speed=%d, Angle=%d, CW=%d\n", speed, angle, cw);
}

void stopMotor() {
  Serial.println("[MOTOR] Stop");
  // стоп/удержание
}

void holdOn() {
  holdMode = true;
  stopMotor();
  if (scanning) {
    scanning = false;
    Serial.println("[SCAN] выкл. режим сканирования (HOLD)");
    ws.textAll("{\"cmd\":\"scanState\",\"val\":0}"); // отщёлкнуть кнопку SCAN на странице
  }
  Serial.println("[HOLD] вкл. удержание");
}


// --- Обработчики ---
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
  Serial.printf("[WS] Client #%u connected\n", client->id());
  return;
}
  if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->opcode == WS_TEXT) {
      data[len] = 0;
      String msg = String((char*)data);
      Serial.println("WS: " + msg);

      // Парсим JSON
      DynamicJsonDocument doc(256);
      DeserializationError err = deserializeJson(doc, msg);
      if (!err) {
        String cmd = doc["cmd"].as<String>();
        float val  = doc["val"].as<float>();

        if (cmd == "cw")        { holdOff(false); rotate(val, true); }
        else if (cmd == "ccw")  { holdOff(false); rotate(val, false); }
        else if (cmd == "scan") { holdOff(false); val ? scanStart() : scanStop(); }
        else if (cmd == "hold") {
          if (val) { holdOn(); }
          else     { holdOff(true); }
        }
        else if (cmd == "sspeed") { scanSpeed = val; scanUpdateParams(); }
        else if (cmd == "sangle") { scanAngle = val; scanUpdateParams(); }
        else if (cmd == "micro") { microMode = (val == 1);
                                   Serial.printf("[MICRO] %s\n", microMode ? "ON" : "OFF"); }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[Rotator] Boot");

  WiFi.softAP(ssid, password);
  WiFi.setSleep(false);
  Serial.print("[Rotator] AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("[HTTP] Page requested");
    request->send_P(200, "text/html; charset=utf-8", index_html);
  });

  ws.onEvent(onWsEvent);      // <-- привязка обработчика
  server.addHandler(&ws);     // <-- регистрация WebSocket на /ws
  server.begin();
  Serial.println("[Rotator] Web server started");
}


void loop() {
    // --- Виртуальное сканирование ---
  if (scanning && !holdMode) {
    uint32_t now = millis();
    float dt = (now - lastScanMs) / 1000.0f;
    lastScanMs = now;

    float half = scanAngle / 2.0f;
    scanPos += scanDir * scanSpeed * dt;

    if (scanDir == 1 && scanPos >= half) {          // только при движении вперёд
      scanPos = half;
      scanDir = -1;
      Serial.printf("[SCAN] край +%.0f град — разворот назад (скорость %.0f град/сек)\n",
                    half, scanSpeed);
    } else if (scanDir == -1 && scanPos <= -half) { // только при движении назад
      scanPos = -half;
      scanDir = 1;
      Serial.printf("[SCAN] край -%.0f град — разворот вперёд (скорость %.0f град/сек)\n",
                    half, scanSpeed);
    }
  } else {
    lastScanMs = millis();
  }

}
