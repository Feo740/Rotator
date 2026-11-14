#ifndef WEB_PAGE_H
#define WEB_PAGE_H

#include <pgmspace.h>

// --- HTML + JS (встроенная страница) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="UTF-8">
  <title>Rotator Control</title>
  <style>
    body { font-family: Arial, sans-serif; padding: 16px; }
    .controls { max-width: 400px; margin: 0 auto; }
    button { width: 45%; padding: 12px; font-size: 14px; margin: 4px 2%; }
    .btn-cw { background: #4caf50; color: white; }
    .btn-ccw { background: #f44336; color: white; }
    .btn-scan.active { background: #ff9800; color: #fff; }
    .btn-hold.active { background: #f44336; color: #fff; }
    label { display: block; margin-top: 12px; }
    input[type=range] { width: 100%; }
  </style>
</head>
<body>
  <div class="controls">
    <h3>Rotator Control</h3>

    <button id="btn-cw" class="btn-cw">CW (По часовой)</button>
    <button id="btn-ccw" class="btn-ccw">CCW (Против)</button>

    <div class="slider-block">
      <label>Угол поворота, град. <span id="angle-val">5</span></label>
      <input type="range" id="angle" min="3" max="15" value="5" step="1">
    </div>

    <div class="switch-row">
      <span>Микрошаг</span>
      <label class="switch">
        <input type="checkbox" id="microstep" onchange="send('micro', this.checked ? 1 : 0)">
        <span class="slider-sw"></span>
      </label>
    </div>

    <div class="slider-block">
      <label>Величина микрошага, град. <span id="ustep-val">0.5</span></label>
      <input type="range" id="ustep" min="5" max="30" value="5" step="1">
    </div>

    <button id="btn-scan" class="btn-scan">SCAN (Сканирование)</button>
    <button id="btn-hold" class="btn-hold">HOLD (Стоп/Удержание)</button>

        <div class="slider-block">
      <label>Скорость сканирования, град/сек <span id="sspeed-val">10</span></label>
      <input type="range" id="sspeed" min="1" max="60" value="10" step="1">
    </div>

    <div class="slider-block">
      <label>Угол сканирования, град. <span id="sangle-val">90</span></label>
      <input type="range" id="sangle" min="45" max="270" value="90" step="1">
    </div>
  </div>


  <script>
  var ws = new WebSocket('ws://' + window.location.hostname + '/ws');

  ws.onopen = function() { console.log('WS connected'); };
  ws.onclose = function() { console.log('WS closed'); };
  ws.onmessage = function(evt) {
      try {
        var m = JSON.parse(evt.data);
        if (m.cmd === 'holdState') {
          document.getElementById('btn-hold').classList.toggle('active', m.val == 1);
        }

        if (m.cmd === 'scanState') {
          document.getElementById('btn-scan').classList.toggle('active', m.val == 1);
        }
      } catch(e) {}
    };


  function send(cmd, val) {
    var msg = JSON.stringify({cmd: cmd, val: val});
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(msg);
    } else {
      console.log('WS not ready:', msg);
    }
  }

  function sendClick(cmd) {
    var micro = document.getElementById('microstep').checked;
    var val;
    if (micro) {
      val = parseInt(document.getElementById('ustep').value) / 10; // 0.5 ... 3.0
    } else {
      val = parseInt(document.getElementById('angle').value);      // 3 ... 15
    }
    send(cmd, val);
  }

  function bindButtons() {
    document.getElementById('btn-cw').addEventListener('click', function(e) {
      e.preventDefault(); sendClick('cw');
    });
    document.getElementById('btn-ccw').addEventListener('click', function(e) {
      e.preventDefault(); sendClick('ccw');
    });
    // SCAN — тумблер с фиксацией
    document.getElementById('btn-scan').addEventListener('click', function(e) {
      e.preventDefault();
      var btn = this;
      var on = btn.classList.toggle('active');
      send('scan', on ? 1 : 0);
    });
    document.getElementById('btn-hold').addEventListener('click', function(e) {
      e.preventDefault(); send('hold', 0);
    });

    // Ползунки сканирования — шлют значение сразу при каждом сдвиге
    var sspeed = document.getElementById('sspeed');
    sspeed.addEventListener('input', function(e) {
      document.getElementById('sspeed-val').innerText = e.target.value;
      send('sspeed', parseInt(e.target.value));
    });

    var sangle = document.getElementById('sangle');
    sangle.addEventListener('input', function(e) {
      document.getElementById('sangle-val').innerText = e.target.value;
      send('sangle', parseInt(e.target.value));
    });

    var angle = document.getElementById('angle');
    angle.addEventListener('input', function(e) {
      document.getElementById('angle-val').innerText = e.target.value;
    });

    var ustep = document.getElementById('ustep');
    ustep.addEventListener('input', function(e) {
      document.getElementById('ustep-val').innerText = (parseInt(e.target.value) / 10).toFixed(1);
    });
  }

  bindButtons();
</script>

</body>
</html>
)rawliteral";
#endif // WEB_PAGE_H