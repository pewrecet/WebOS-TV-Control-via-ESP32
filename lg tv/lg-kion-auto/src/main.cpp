// ============================================================================
//  LG + КИОН автоматизация на ESP32
//
//  Что делает:
//   1. Коннектится к Wi-Fi
//   2. Пингует TV. OFF -> ON = триггер
//   3. Подключается по WebOS SSAP (ws://TV:3000)
//   4. Запускает приложение КИОН
//   5. Шлёт запрограммированную последовательность кнопок пульта
//   6. Отключается, возвращается к мониторингу
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "AppLog.h"
#include "WebOSClient.h"
#include "TvDetector.h"
#include "KeyStore.h"

#define Serial Log

static WebOSClient tv;
static TvDetector  detector;

// ---- Конечный автомат высокого уровня ----
enum class AppState {
    IDLE,                   // ждём включения TV
    WAIT_BOOT,              // TV включился, ждём загрузку WebOS
    CONNECTING,             // SSAP handshake
    LAUNCHED,               // отправили launch
    RUNNING_SEQUENCE,       // шлём кнопки
    COOLDOWN                // выполнили, отдыхаем пока TV не выключат
};
static AppState        appState    = AppState::IDLE;
static uint32_t        stateSince  = 0;
static size_t          seqIndex    = 0;
static uint32_t        nextStepAt  = 0;
static bool            waitingForPointerLog = false;
static bool            launchAccepted = false;
static bool            launchFailed = false;
static size_t          launchCandidateIndex = 0;

static const char* const APP_LAUNCH_CANDIDATES[] = {
    APP_ID_KION,
    APP_ID_KION_ALT,
};
static const size_t APP_LAUNCH_CANDIDATES_LEN =
    sizeof(APP_LAUNCH_CANDIDATES) / sizeof(APP_LAUNCH_CANDIDATES[0]);

static void setState(AppState s) {
    appState   = s;
    stateSince = millis();
}

static bool launchCurrentCandidate() {
    if (launchCandidateIndex >= APP_LAUNCH_CANDIDATES_LEN) {
        return false;
    }

    const char* appId = APP_LAUNCH_CANDIDATES[launchCandidateIndex];
    Serial.printf("[App] SSAP ready, launching candidate %u/%u: %s\n",
                  (unsigned)(launchCandidateIndex + 1),
                  (unsigned)APP_LAUNCH_CANDIDATES_LEN,
                  appId);

    return tv.launchApp(appId);
}

// ---- Wi-Fi ----
static void wifiConnect() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial.print('.');
        if (millis() - t0 > 30000) {
            Serial.println(" TIMEOUT, restarting");
            ESP.restart();
        }
    }
    Serial.printf(" OK, IP=%s\n", WiFi.localIP().toString().c_str());
}

// ---- Обработка последовательности кнопок ----
static void resetSequence() {
    seqIndex   = 0;
    nextStepAt = 0;
}

static void runSequenceStep() {
    if (seqIndex >= REMOTE_SEQUENCE_LEN) {
        Serial.println("[Seq] Done!");
        tv.disconnect();
        setState(AppState::COOLDOWN);
        return;
    }

    const ButtonStep& step = REMOTE_SEQUENCE[seqIndex];
    if (step.button != nullptr) {
        tv.sendButton(step.button);
    } else {
        Serial.printf("[Seq] Pause %u ms\n", step.delayAfterMs);
    }
    nextStepAt = millis() + step.delayAfterMs;
    seqIndex++;
}

// ---- Setup ----
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);
    Serial.println("\n=== LG + KION automation on ESP32 ===");
    Serial.printf("[Build] TV_PORT=%u DETECT_TCP_PORT=%u DEBUG_APPS=%d DEBUG_LAUNCH_POINTS=%d DEBUG_FG_APP=%d\n",
                  (unsigned)TV_PORT,
                  (unsigned)DETECT_TCP_PORT,
                  (int)DEBUG_LIST_APPS_ON_READY,
                  (int)DEBUG_LIST_LAUNCH_POINTS_ON_READY,
                  (int)DEBUG_WATCH_FOREGROUND_APP_ON_READY);

    wifiConnect();
    Log.beginHttpServer();

    // client-key: сначала пробуем NVS, потом compile-time
    String key = KeyStore::load();
    if (key.length() == 0 && strlen(TV_CLIENT_KEY) > 0) {
        key = TV_CLIENT_KEY;
        Serial.println("[KeyStore] Using key from config.h");
    } else if (key.length() > 0) {
        Serial.println("[KeyStore] Using key from NVS");
    } else {
        Serial.println("[KeyStore] No client-key yet, TV will prompt on screen");
    }

    tv.begin(TV_IP, TV_PORT, TV_USE_TLS, key.c_str());

    tv.onNewClientKey([](const String& newKey) {
        Serial.println("[KeyStore] Got new client-key, saving to NVS");
        KeyStore::save(newKey);
    });

    tv.onInvalidClientKey([]() {
        Serial.println("[KeyStore] Clearing stale client-key, rebooting to re-pair");
        KeyStore::clear();
        delay(500);
        ESP.restart();
    });

    tv.onReady([]() {
        if (DEBUG_LIST_APPS_ON_READY) {
            Serial.println("[App] SSAP ready, requesting app list...");
            setState(AppState::COOLDOWN);
            tv.listApps();
            return;
        }

        if (DEBUG_LIST_LAUNCH_POINTS_ON_READY) {
            Serial.println("[App] SSAP ready, requesting launch points instead of launching app");
            setState(AppState::COOLDOWN);
            tv.listLaunchPoints();
            return;
        }

        if (DEBUG_WATCH_FOREGROUND_APP_ON_READY) {
            Serial.println("[App] SSAP ready, watching foreground app changes");
            setState(AppState::COOLDOWN);
            tv.subscribeForegroundAppInfo(true);
            return;
        }

        launchCandidateIndex = 0;
        launchAccepted = false;
        launchFailed = false;
        if (!launchCurrentCandidate()) {
            Serial.println("[App] launchApp send failed");
            launchAccepted = false;
            launchFailed = true;
            tv.disconnect();
            setState(AppState::COOLDOWN);
            return;
        }

        waitingForPointerLog = false;
        setState(AppState::LAUNCHED);
    });

    tv.onLaunchResult([](bool success, const String& error) {
        if (success) {
            Serial.println("[App] Launch confirmed by TV");
            launchAccepted = true;
            launchFailed = false;
            return;
        }

        if (launchCandidateIndex + 1 < APP_LAUNCH_CANDIDATES_LEN) {
            launchCandidateIndex++;
            Serial.printf("[App] Launch failed, trying next candidate: %s\n",
                          APP_LAUNCH_CANDIDATES[launchCandidateIndex]);
            launchAccepted = false;
            launchFailed = false;
            if (launchCurrentCandidate()) {
                return;
            }
        }

        Serial.printf("[App] Launch failed: %s\n", error.c_str());
        launchAccepted = false;
        launchFailed = true;
    });

    tv.onDisconnected([]() {
        if (appState == AppState::CONNECTING ||
            appState == AppState::LAUNCHED   ||
            appState == AppState::RUNNING_SEQUENCE) {
            Serial.println("[App] Unexpected disconnect");
            waitingForPointerLog = false;
            launchAccepted = false;
            launchFailed = false;
            setState(AppState::COOLDOWN);
        }
    });

    detector.begin(TV_IP, PING_INTERVAL_MS, PING_TIMEOUT_MS,
                   PING_ATTEMPTS, DETECT_METHOD_PING, DETECT_TCP_PORT);

    setState(AppState::IDLE);
    Serial.println("[App] Monitoring TV...");
}

// ---- Loop ----
void loop() {
    // Wi-Fi watchdog
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Lost, reconnecting");
        WiFi.reconnect();
        delay(2000);
        return;
    }

    tv.loop();
    Log.loop();

    switch (appState) {
        case AppState::IDLE: {
            if (detector.poll()) {
                Serial.println("[App] TV turned ON — waiting for WebOS boot");
                setState(AppState::WAIT_BOOT);
            }
            break;
        }

        case AppState::WAIT_BOOT: {
            if (millis() - stateSince >= TV_BOOT_DELAY_MS) {
                Serial.println("[App] Connecting to SSAP");
                tv.connect();
                setState(AppState::CONNECTING);
            }
            break;
        }

        case AppState::CONNECTING: {
            // Таймаут подключения 20 сек
            if (millis() - stateSince > 20000 && !tv.isRegistered()) {
                Serial.println("[App] Connect timeout, giving up");
                tv.disconnect();
                setState(AppState::COOLDOWN);
            }
            break;
        }

        case AppState::LAUNCHED: {
            detector.poll();

            if (launchFailed) {
                Serial.println("[App] Aborting sequence because app launch failed");
                tv.disconnect();
                waitingForPointerLog = false;
                setState(AppState::COOLDOWN);
                break;
            }

            if (!tv.isPointerConnected()) {
                if (!waitingForPointerLog) {
                    Serial.println("[App] Waiting for pointer socket...");
                    waitingForPointerLog = true;
                }
                if (millis() - stateSince > APP_LAUNCH_DELAY_MS + 15000) {
                    Serial.println("[App] Pointer socket timeout, aborting");
                    tv.disconnect();
                    setState(AppState::COOLDOWN);
                }
                break;
            }

            if (!launchAccepted) {
                if (millis() - stateSince > APP_LAUNCH_DELAY_MS + 15000) {
                    Serial.println("[App] Launch confirmation timeout, aborting");
                    tv.disconnect();
                    waitingForPointerLog = false;
                    setState(AppState::COOLDOWN);
                }
                break;
            }

            if (millis() - stateSince < APP_LAUNCH_DELAY_MS) {
                break;
            }

            Serial.println("[App] Pointer ready, starting remote sequence");
            resetSequence();
            nextStepAt = millis();
            waitingForPointerLog = false;
            setState(AppState::RUNNING_SEQUENCE);
            break;
        }

        case AppState::RUNNING_SEQUENCE: {
            if (millis() >= nextStepAt) {
                runSequenceStep();
            }
            // Периодически всё равно опрашиваем детектор — на случай если TV вырубили
            detector.poll();
            break;
        }

        case AppState::COOLDOWN: {
            // Ждём, пока TV станет OFF, только тогда готовы снова триггериться
            detector.poll();
            if (detector.state() == TvDetector::State::OFF) {
                Serial.println("[App] TV is OFF again, back to monitoring");
                waitingForPointerLog = false;
                launchAccepted = false;
                launchFailed = false;
                setState(AppState::IDLE);
            }
            break;
        }

        default: break;
    }

    delay(10);  // не душим CPU
}
