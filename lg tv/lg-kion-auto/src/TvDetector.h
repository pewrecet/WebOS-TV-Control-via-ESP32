#pragma once

#include <Arduino.h>
#include <IPAddress.h>

// Детектор состояния TV.
// Два режима:
//   PING — ICMP эхо. Просто, но многие LG отвечают и в Standby.
//   TCP  — пробуем открыть TCP-порт 3000 (WebOS API). Работает только когда
//          TV реально включён и WebOS загружен.
class TvDetector {
public:
    enum class State { UNKNOWN, OFF, ON };

    void begin(const char* ip, uint32_t intervalMs, uint32_t timeoutMs,
               uint8_t attempts, bool usePing, uint16_t tcpPort);

    // Вызывать в loop(). Возвращает true, если произошёл переход OFF -> ON.
    bool poll();

    State state() const { return _state; }

private:
    IPAddress _ip;
    uint32_t  _intervalMs = 3000;
    uint32_t  _timeoutMs  = 1000;
    uint8_t   _attempts   = 2;
    bool      _usePing    = true;
    uint16_t  _tcpPort    = 3000;
    uint32_t  _lastCheck  = 0;
    State     _state      = State::UNKNOWN;

    bool probe();
};
