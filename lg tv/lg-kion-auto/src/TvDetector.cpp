#include <Arduino.h>
#include "TvDetector.h"
#include "AppLog.h"
#include <WiFi.h>
#include <ESP32Ping.h>

#define Serial Log

void TvDetector::begin(const char* ip, uint32_t intervalMs, uint32_t timeoutMs,
                       uint8_t attempts, bool usePing, uint16_t tcpPort) {
    _ip.fromString(ip);
    _intervalMs = intervalMs;
    _timeoutMs  = timeoutMs;
    _attempts   = attempts;
    _usePing    = usePing;
    _tcpPort    = tcpPort;
    _state      = State::UNKNOWN;
    _lastCheck  = 0;

    if (_usePing) {
        Serial.printf("[TV] Detector mode: PING %s\n", _ip.toString().c_str());
    } else {
        Serial.printf("[TV] Detector mode: TCP %s:%u\n", _ip.toString().c_str(), _tcpPort);
    }
}

bool TvDetector::probe() {
    if (_usePing) {
        // ESP32Ping.ping возвращает bool (успех по агрегату попыток)
        return Ping.ping(_ip, _attempts);
    } else {
        // TCP-пробник: TV отвечает на 3000 только когда включён и WebOS поднят
        WiFiClient c;
        c.setTimeout(_timeoutMs / 1000 + 1);
        bool ok = c.connect(_ip, _tcpPort, _timeoutMs);
        c.stop();
        return ok;
    }
}

bool TvDetector::poll() {
    uint32_t now = millis();
    if (now - _lastCheck < _intervalMs) return false;
    _lastCheck = now;

    bool alive = probe();
    State prev = _state;
    _state = alive ? State::ON : State::OFF;

    if (prev != _state) {
        Serial.printf("[TV] State: %s -> %s\n",
                      prev == State::UNKNOWN ? "UNKNOWN" : (prev == State::ON ? "ON" : "OFF"),
                      _state == State::ON ? "ON" : "OFF");
    }

    // Триггер только при реальном переходе OFF/UNKNOWN -> ON
    // (UNKNOWN -> ON на старте тоже считаем переходом — если при запуске ESP
    // TV уже включён, автоматизация не сработает, что в большинстве случаев
    // и хотим. Если нужно иначе, поменяй условие на prev == State::OFF).
    return prev == State::OFF && _state == State::ON;
}
