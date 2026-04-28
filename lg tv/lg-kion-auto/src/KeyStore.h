#pragma once

#include <Arduino.h>
#include <Preferences.h>

// Хранение client-key в NVS (Non-Volatile Storage) ESP32.
// Если в config.h ключ пустой, TV выдаст новый при pairing, и мы его сохраним.
// При следующих загрузках читаем из NVS, а не просим подтверждения заново.
class KeyStore {
public:
    static constexpr const char* NS  = "lgauto";
    static constexpr const char* KEY = "client_key";

    static String load() {
        Preferences p;
        if (!p.begin(NS, true)) return String();
        String v = p.getString(KEY, "");
        p.end();
        return v;
    }

    static bool save(const String& key) {
        Preferences p;
        if (!p.begin(NS, false)) return false;
        bool ok = p.putString(KEY, key) > 0;
        p.end();
        return ok;
    }

    static bool clear() {
        Preferences p;
        if (!p.begin(NS, false)) return false;
        bool ok = p.remove(KEY);
        p.end();
        return ok;
    }
};
