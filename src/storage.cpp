#include "storage.h"

Preferences prefs;

void saveBaseParams() {
  prefs.begin("rotator", false);          // false = режим записи
  prefs.putInt("angle",  currentAngle);
  prefs.putInt("ustep",  (int)(currentUstep * 10));
  prefs.putBool("micro", microMode);
  prefs.putInt("sspeed", (int)scanSpeed);
  prefs.putInt("sangle", (int)scanAngle);
  prefs.end();
  Serial.println("[NVS] Параметры сохранены как базовые");
}

void loadBaseParams(bool toSerialOnly) {
  prefs.begin("rotator", true);           // true = только чтение
  int  a  = prefs.getInt("angle",  5);    // значения по умолчанию для первого запуска
  int  u  = prefs.getInt("ustep",  5);
  bool m  = prefs.getBool("micro", false);
  int  sp = prefs.getInt("sspeed", 10);
  int  sa = prefs.getInt("sangle", 90);
  prefs.end();

  // применяем к состоянию платы
  currentAngle = a;
  currentUstep = u / 10.0f;
  microMode    = m;
  scanSpeed    = sp;
  scanAngle    = sa;

  // отправляем на страницу, чтобы ползунки встали на место
  // ustep в JSON — целое (5..30), страница делит на 10 сама
  char buf[160];
  snprintf(buf, sizeof(buf),
           "{\"cmd\":\"params\",\"angle\":%d,\"ustep\":%d,\"micro\":%d,\"sspeed\":%d,\"sangle\":%d}",
           a, u, m ? 1 : 0, sp, sa);
  ws.textAll(buf);

  if (!toSerialOnly) {
    Serial.printf("[NVS] Загружены базовые: угол %d, микрошаг %.1f, скорость %d, размах %d\n",
                  a, currentUstep, sp, sa);
  }
}
