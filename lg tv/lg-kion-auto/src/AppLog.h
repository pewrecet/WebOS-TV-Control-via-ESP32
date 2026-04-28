#pragma once

#include <Arduino.h>

class AppLogPort : public Print {
public:
    AppLogPort();

    void begin(unsigned long baud);
    void beginHttpServer();
    void loop();

    String localUrl() const;
    String snapshot() const;
    void clear();

    int printf(const char* format, ...) __attribute__((format(printf, 2, 3)));

    size_t write(uint8_t ch) override;
    size_t write(const uint8_t* buffer, size_t size) override;
    using Print::write;

private:
    static constexpr size_t MAX_LOG_BYTES = 12288;

    class Impl;
    Impl* _impl = nullptr;

    void trimIfNeeded();
    void appendChunk(const char* data, size_t len);
    String escapeHtml(const String& text) const;
};

extern AppLogPort Log;
