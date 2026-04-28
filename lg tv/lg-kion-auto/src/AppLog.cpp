#include "AppLog.h"

#include <WebServer.h>
#include <WiFi.h>
#include <memory>
#include <stdarg.h>

class AppLogPort::Impl {
public:
    WebServer server{80};
    String buffer;
    bool serialStarted = false;
    bool httpStarted = false;
};

AppLogPort Log;

AppLogPort::AppLogPort() {
    _impl = new Impl();
    _impl->buffer.reserve(MAX_LOG_BYTES + 256);
}

void AppLogPort::begin(unsigned long baud) {
    if (_impl->serialStarted) return;
    ::Serial.begin(baud);
    _impl->serialStarted = true;
}

void AppLogPort::beginHttpServer() {
    if (_impl->httpStarted) return;

    _impl->server.on("/", HTTP_GET, [this]() {
        String html;
        String logs = escapeHtml(snapshot());

        html.reserve(logs.length() + 768);
        html += F(
            "<!doctype html><html><head><meta charset='utf-8'>"
            "<meta http-equiv='refresh' content='2'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<title>ESP32 Log Monitor</title>"
            "<style>"
            "body{margin:0;font-family:Consolas,Monaco,monospace;background:#111;color:#e8e8e8;}"
            "header{padding:14px 16px;background:#1d1d1d;border-bottom:1px solid #333;}"
            "a{color:#7dd3fc;text-decoration:none;}"
            "main{padding:16px;}"
            "pre{white-space:pre-wrap;word-break:break-word;background:#161616;border:1px solid #333;"
            "padding:12px;border-radius:8px;min-height:60vh;}"
            ".meta{color:#9ca3af;font-size:14px;margin-top:6px;}"
            "</style></head><body><header><strong>ESP32 Log Monitor</strong><div class='meta'>");
        html += F("Auto-refresh every 2 seconds. Raw text: <a href='/logs'>/logs</a>");
        html += F("</div></header><main><pre>");
        html += logs;
        html += F("</pre></main></body></html>");

        _impl->server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
        _impl->server.send(200, "text/html; charset=utf-8", html);
    });

    _impl->server.on("/logs", HTTP_GET, [this]() {
        _impl->server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
        _impl->server.send(200, "text/plain; charset=utf-8", snapshot());
    });

    _impl->server.on("/clear", HTTP_POST, [this]() {
        clear();
        _impl->server.send(200, "text/plain; charset=utf-8", "OK\n");
    });

    _impl->server.on("/favicon.ico", HTTP_GET, [this]() {
        _impl->server.send(204, "text/plain", "");
    });

    _impl->server.begin();
    _impl->httpStarted = true;

    String url = localUrl();
    if (url.length() > 0) {
        printf("[Log] HTTP monitor: %s\n", url.c_str());
    } else {
        printf("[Log] HTTP monitor started on port 80\n");
    }
}

void AppLogPort::loop() {
    if (_impl->httpStarted) {
        _impl->server.handleClient();
    }
}

String AppLogPort::localUrl() const {
    if (WiFi.status() != WL_CONNECTED) {
        return "";
    }

    String url = "http://";
    url += WiFi.localIP().toString();
    url += "/";
    return url;
}

String AppLogPort::snapshot() const {
    return _impl->buffer;
}

void AppLogPort::clear() {
    _impl->buffer = "";
}

int AppLogPort::printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list argsCopy;
    va_copy(argsCopy, args);
    int needed = vsnprintf(nullptr, 0, format, argsCopy);
    va_end(argsCopy);

    if (needed <= 0) {
        va_end(args);
        return needed;
    }

    std::unique_ptr<char[]> buffer(new char[needed + 1]);
    vsnprintf(buffer.get(), needed + 1, format, args);
    va_end(args);

    write((const uint8_t*)buffer.get(), needed);
    return needed;
}

size_t AppLogPort::write(uint8_t ch) {
    return write(&ch, 1);
}

size_t AppLogPort::write(const uint8_t* buffer, size_t size) {
    if (size == 0) return 0;

    ::Serial.write(buffer, size);
    appendChunk((const char*)buffer, size);
    return size;
}

void AppLogPort::trimIfNeeded() {
    if (_impl->buffer.length() <= MAX_LOG_BYTES) return;

    size_t overflow = _impl->buffer.length() - MAX_LOG_BYTES;
    int cutAt = _impl->buffer.indexOf('\n', overflow);
    if (cutAt >= 0) {
        _impl->buffer.remove(0, cutAt + 1);
    } else {
        _impl->buffer.remove(0, overflow);
    }
}

void AppLogPort::appendChunk(const char* data, size_t len) {
    if (len == 0) return;
    size_t oldLength = _impl->buffer.length();
    _impl->buffer.reserve(oldLength + len + 1);
    for (size_t i = 0; i < len; ++i) {
        _impl->buffer += data[i];
    }
    trimIfNeeded();
}

String AppLogPort::escapeHtml(const String& text) const {
    String escaped;
    escaped.reserve(text.length() + 128);

    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        switch (c) {
            case '&': escaped += F("&amp;"); break;
            case '<': escaped += F("&lt;"); break;
            case '>': escaped += F("&gt;"); break;
            default:  escaped += c; break;
        }
    }

    return escaped;
}
