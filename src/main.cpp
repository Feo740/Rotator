#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "web_page.h"      // страница здесь
#include "storage.h"       // NVS-параметры
#include "motor.h"         // драйвер TMC2160 (STEP/DIR/EN)

// --- Настройки Wi-Fi ---
const char* ssid = "Rotator";
const char* password = "12345678";

// --- Глобальные переменные ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

int   currentAngle = 5;
float currentUstep = 0.5f;
bool  microMode    = false;   // режим микрошага (для лога/будущей логики)

bool  holdMode = false;

// --- Сканирование ---
bool     scanning  = false;
float    scanSpeed = 10.0;   // град/сек
float    scanAngle = 90.0;   // полный размах, град
float    scanPos   = 0.0;    // смещение от центра сканирования, град
float    scanCenterPos = 0.0; // позиция вала на момент включения сканирования
int      scanDir   = 1;      // 1 = вперёд, -1 = назад
uint32_t lastScanMs = 0;

void scanStart() {
  scanning = true;
  scanPos = 0.0;
  scanCenterPos = posDeg;              // запоминаем, от какой точки начали
  scanDir = 1;
  motorSetDir(true);
  pulseStart(degPerSecToFreq(scanSpeed));
  Serial.printf("[SCAN] вкл. режим сканирования скорость %.0f град/сек, угол %.0f град\n",
                scanSpeed, scanAngle);
}

void scanStop() {
  scanning = false;
  pulseStop();
  Serial.println("[SCAN] выкл. режим сканирования");
}

void scanUpdateParams() {
  if (scanning) {
    scanApplySpeed();                  // частота STEP меняется на лету
    Serial.printf("[SCAN] новые параметры: скорость %.0f град/сек, угол %.0f град\n",
                  scanSpeed, scanAngle);
    // если положение вышло за новый размах — вернём в пределы
    float half = scanAngle / 2.0f;
    if (scanPos > half)  scanPos = half;
    if (scanPos < -half) scanPos = -half;
  }
}

void holdOff(bool byUser) {
  if (holdMode) {
    holdMode = false;
    Serial.println(byUser ? "[HOLD] выкл. удержание" : "[HOLD] удержание снято командой движения");
    ws.textAll("{\"cmd\":\"holdState\",\"val\":0}"); // снять фиксацию кнопки HOLD на странице
  }
}

void holdOn() {
  holdMode = true;
  stopMotor();   // импульсы остановлены; EN остаётся включён — вал удерживается током бесплатно
  if (scanning) {
    scanning = false;
    Serial.println("[SCAN] выкл. режим сканирования (HOLD)");
    ws.textAll("{\"cmd\":\"scanState\",\"val\":0}");
  }
  Serial.println("[HOLD] вкл. удержание");
}

// --- Обработчики ---
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] Client #%u connected\n", client->id());
    loadBaseParams(true);   // страница при подключении получает актуальные параметры
    return;
  }
  if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->opcode == WS_TEXT) {
      data[len] = 0;
      String msg = String((char*)data);
      Serial.println("WS: " + msg);

      DynamicJsonDocument doc(256);
      DeserializationError err = deserializeJson(doc, msg);
      if (!err) {
        String cmd = doc["cmd"].as<String>();
        float val  = doc["val"].as<float>();

        if (cmd == "cw")        { holdOff(false); rotate(val, true); }
        else if (cmd == "ccw")  { holdOff(false); rotate(val, false); }
        else if (cmd == "scan") { holdOff(false); val ? scanStart() : scanStop(); }
        else if (cmd == "hold") { val ? holdOn() : holdOff(true); }
        else if (cmd == "sspeed") { scanSpeed = val; scanUpdateParams(); }
        else if (cmd == "sangle") { scanAngle = val; scanUpdateParams(); }
        else if (cmd == "micro")  { microMode = (val == 1);
                                    Serial.printf("[MICRO] %s\n", microMode ? "ON" : "OFF"); }
        else if (cmd == "angle")      { currentAngle = (int)val; }
        else if (cmd == "ustep")      { currentUstep = val; }
        else if (cmd == "saveParams") { saveBaseParams(); }
        else if (cmd == "loadParams") { loadBaseParams(false); }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[Rotator] Boot");

  motorInit();
  motorEnable(true);       // включаем драйвер: вал удерживается током обмоток
  Serial.println("[MOTOR] Готов (STEP/DIR/EN)");

  WiFi.softAP(ssid, password);
  WiFi.setSleep(false);
  Serial.print("[Rotator] AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("[HTTP] Page requested");
    request->send_P(200, "text/html; charset=utf-8", index_html);
  });

  loadBaseParams(true);   // применяем сохранённые значения без лишнего лога

  ws.onEvent(onWsEvent);      // привязка обработчика
  server.addHandler(&ws);     // регистрация WebSocket на /ws
  server.begin();
  Serial.println("[Rotator] Web server started");
}

void loop() {
  // --- Ручной поворот: остановка по времени (частота известна точно) ---
  if (moving) {
    float needMs = ((float)stepsToGo / (float)ROTATE_FREQ) * 1000.0f;
    if (millis() - rotStartMs >= needMs) stopMotor();
  }

  // --- Сканирование ---
  if (scanning && !holdMode) {
    uint32_t now = millis();
    float dt = (now - lastScanMs) / 1000.0f;
    lastScanMs = now;

    float half = scanAngle / 2.0f;
    scanPos += scanDir * scanSpeed * dt;
    posDeg = scanCenterPos + scanPos;   // реальная позиция вала

    if (scanDir == 1 && scanPos >= half) {          // только при движении вперёд
      scanPos = half;
      scanDir = -1;
      scanReverse(false);
      Serial.printf("[SCAN] край +%.0f град — разворот назад (скорость %.0f град/сек)\n",
                    half, scanSpeed);
    } else if (scanDir == -1 && scanPos <= -half) { // только при движении назад
      scanPos = -half;
      scanDir = 1;
      scanReverse(true);
      Serial.printf("[SCAN] край -%.0f град — разворот вперёд (скорость %.0f град/сек)\n",
                    half, scanSpeed);
    }
  } else {
    lastScanMs = millis();
  }
}
