#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// --- Настройки Wi‑Fi ---
const char* ssid = "Rotator";
const char* password = "12345678";

// --- Глобальные переменные ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

int motorSpeed = 50;      // 0–100%
int scanAngle = 60;       // градусы
bool isHolding = false;

// --- Заглушки для управления мотором (сюда позже вставишь TMC2160) ---
void moveMotor(int speed, int angle, bool cw) {
  // Здесь будет логика шаговика и TMC2160
  Serial.printf("[MOTOR] Speed=%d, Angle=%d, CW=%d\n", speed, angle, cw);
}

void stopMotor() {
  Serial.println("[MOTOR] Stop");
  // стоп/удержание
}

// --- HTML + JS (встроенная страница) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Rotator Control</title>
  <style>
    body { font-family: Arial, sans-serif; padding: 16px; }
    .controls { max-width: 400px; margin: 0 auto; }
    button { width: 45%; padding: 12px; font-size: 14px; margin: 4px 2%; }
    .btn-cw { background: #4caf50; color: white; }
    .btn-ccw { background: #f44336; color: white; }
    .btn-scan { background: #2196f3; color: white; }
    .btn-hold { background: #9e9e9e; color: white; }
    label { display: block; margin-top: 12px; }
    input[type=range] { width: 100%; }
  </style>
</head>
<body>
  <div class="controls">
    <h3>Rotator Control</h3>
    <button id="btn-cw" class="btn-cw">CW (По часовой)</button>
    <button id="btn-ccw" class="btn-ccw">CCW (Против)</button>
    <button id="btn-scan" class="btn-scan">SCAN (Сканирование)</button>
    <button id="btn-hold" class="btn-hold">HOLD (Стоп/Удержание)</button>

    <label>Скорость (%): <span id="speed-val">50</span></label>
    <input type="range" id="speed" min="0" max="100" value="50">

    <label>Угол сканирования (°): <span id="angle-val">60</span></label>
    <input type="range" id="angle" min="10" max="180" value="60">
  </div>

  <script>
    var ws;
    function init() {
      ws = new WebSocket('ws://' + location.host + '/ws');
      ws.onopen = function() { console.log('WS connected'); };
      ws.onclose = function() { console.log('WS closed'); };
    }
    function send(cmd, val) {
      var msg = JSON.stringify({cmd: cmd, val: val});
      if(ws && ws.readyState === WebSocket.OPEN) ws.send(msg);
    }

    document.getElementById('btn-cw').onclick = () => send('cw', 0);
    document.getElementById('btn-ccw').onclick = () => send('ccw', 0);
    document.getElementById('btn-scan').onclick = () => send('scan', 0);
    document.getElementById('btn-hold').onclick = () => send('hold', 0);

    var speed = document.getElementById('speed');
    speed.oninput = (e) => {
      document.getElementById('speed-val').innerText = e.target.value;
      send('speed', parseInt(e.target.value));
    };

    var angle = document.getElementById('angle');
    angle.oninput = (e) => {
      document.getElementById('angle-val').innerText = e.target.value;
      send('angle', parseInt(e.target.value));
    };
  </script>
</body>
</html>
)rawliteral";

// --- Обработчики ---
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
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
        int val = doc["val"];

        if (cmd == "cw") { moveMotor(motorSpeed, scanAngle, true); }
        else if (cmd == "ccw") { moveMotor(motorSpeed, scanAngle, false); }
        else if (cmd == "scan") {
          // Пример: туда‑обратно на scanAngle
          Serial.println("[SCAN] Start");
        }
        else if (cmd == "hold") { stopMotor(); isHolding = true; }
        else if (cmd == "speed") { motorSpeed = val; }
        else if (cmd == "angle") { scanAngle = val; }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[Rotator] Boot");

  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("[Rotator] AP IP: ");
  Serial.println(IP);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
  Serial.printf("[HTTP] Not found: %s\n", request->url().c_str());
  request->send(404, "text/plain", "not found");
});


  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.begin();
  Serial.println("[Rotator] Web server started");
}

void loop() {
  static uint32_t tick = 0;
  tick++;
  //Serial.printf("[Rotator] Tick %lu\n", tick);
  delay(500);  // можно убрать, если нужен быстрый loop
}
