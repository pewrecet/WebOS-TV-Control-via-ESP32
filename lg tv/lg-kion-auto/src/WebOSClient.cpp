#include <Arduino.h>
#include "AppLog.h"
#include "WebOSClient.h"
#include <ArduinoJson.h>

#define Serial Log

// Exact pywebostv registration payload. The signed block must stay byte-stable,
// otherwise the TV may register us with reduced permissions and then reply 401
// to listApps / getPointerInputSocket / launch.
static const char REGISTER_MANIFEST[] PROGMEM = R"JSON(
{
  "forcePairing": false,
  "pairingType": "PROMPT",
  "client-key": "",
  "manifest": {
    "manifestVersion": 1,
    "appVersion": "1.1",
    "signed": {
      "created": "20140509",
      "appId": "com.lge.test",
      "vendorId": "com.lge",
      "localizedAppNames": {
        "": "LG Remote App",
        "ko-KR": "\uB9AC\uBAA8\uCEE8 \uC571",
        "zxx-XX": "\u041B\u0413 R\u044D\u043Cot\u044D A\u041F\u041F"
      },
      "localizedVendorNames": {
        "": "LG Electronics"
      },
      "permissions": [
        "TEST_SECURE",
        "CONTROL_INPUT_TEXT",
        "CONTROL_MOUSE_AND_KEYBOARD",
        "READ_INSTALLED_APPS",
        "READ_LGE_SDX",
        "READ_NOTIFICATIONS",
        "SEARCH",
        "WRITE_SETTINGS",
        "WRITE_NOTIFICATION_ALERT",
        "CONTROL_POWER",
        "READ_CURRENT_CHANNEL",
        "READ_RUNNING_APPS",
        "READ_UPDATE_INFO",
        "UPDATE_FROM_REMOTE_APP",
        "READ_LGE_TV_INPUT_EVENTS",
        "READ_TV_CURRENT_TIME"
      ],
      "serial": "2f930e2d2cfe083771f68e4fe7bb07"
    },
    "permissions": [
      "LAUNCH", "LAUNCH_WEBAPP", "APP_TO_APP", "CLOSE",
      "TEST_OPEN", "TEST_PROTECTED", "CONTROL_AUDIO", "CONTROL_DISPLAY",
      "CONTROL_INPUT_JOYSTICK", "CONTROL_INPUT_MEDIA_RECORDING",
      "CONTROL_INPUT_MEDIA_PLAYBACK", "CONTROL_INPUT_TV", "CONTROL_POWER",
      "READ_APP_STATUS", "READ_CURRENT_CHANNEL", "READ_INPUT_DEVICE_LIST",
      "READ_NETWORK_STATE", "READ_RUNNING_APPS", "READ_TV_CHANNEL_LIST",
      "WRITE_NOTIFICATION_TOAST", "READ_POWER_STATE", "READ_COUNTRY_INFO",
      "READ_SETTINGS", "CONTROL_TV_SCREEN", "CONTROL_TV_STANBY",
      "CONTROL_FAVORITE_GROUP", "CONTROL_USER_INFO",
      "CHECK_BLUETOOTH_DEVICE", "CONTROL_BLUETOOTH", "CONTROL_TIMER_INFO",
      "STB_INTERNAL_CONNECTION", "CONTROL_RECORDING",
      "READ_RECORDING_STATE", "WRITE_RECORDING_LIST", "READ_RECORDING_LIST",
      "READ_RECORDING_SCHEDULE", "WRITE_RECORDING_SCHEDULE",
      "READ_STORAGE_DEVICE_LIST", "READ_TV_PROGRAM_INFO",
      "CONTROL_BOX_CHANNEL", "READ_TV_ACR_AUTH_TOKEN",
      "READ_TV_CONTENT_STATE", "READ_TV_CURRENT_TIME",
      "ADD_LAUNCHER_CHANNEL", "SET_CHANNEL_SKIP", "RELEASE_CHANNEL_SKIP",
      "CONTROL_CHANNEL_BLOCK", "DELETE_SELECT_CHANNEL",
      "CONTROL_CHANNEL_GROUP", "SCAN_TV_CHANNELS",
      "CONTROL_TV_POWER", "CONTROL_WOL"
    ],
    "signatures": [
      {
        "signatureVersion": 1,
        "signature": "eyJhbGdvcml0aG0iOiJSU0EtU0hBMjU2Iiwia2V5SWQiOiJ0ZXN0LXNpZ25pbmctY2VydCIsInNpZ25hdHVyZVZlcnNpb24iOjF9.hrVRgjCwXVvE2OOSpDZ58hR+59aFNwYDyjQgKk3auukd7pcegmE2CzPCa0bJ0ZsRAcKkCTJrWo5iDzNhMBWRyaMOv5zWSrthlf7G128qvIlpMT0YNY+n/FaOHE73uLrS/g7swl3/qH/BGFG2Hu4RlL48eb3lLKqTt2xKHdCs6Cd4RMfJPYnzgvI4BNrFUKsjkcu+WD4OO2A27Pq1n50cMchmcaXadJhGrOqH5YmHdOCj5NSHzJYrsW0HPlpuAx/ECMeIZYDh6RMqaFM2DXzdKX9NmmyqzJ3o/0lkk/N97gfVRLW5hA29yeAwaCViZNCP8iC9aO0q9fQojoa7NQnAtw=="
      }
    ]
  }
}
)JSON";

static bool isPointerButton(const char* name) {
    static const char* list[] = {
        "UP", "DOWN", "LEFT", "RIGHT", "ENTER", "BACK", "EXIT", "HOME",
        "PLAY", "PAUSE", "STOP", "REWIND", "FASTFORWARD",
        "CHANNELUP", "CHANNELDOWN", "CHANNEL_UP", "CHANNEL_DOWN",
        "RED", "GREEN", "YELLOW", "BLUE", "MENU", "INFO", "ASTERISK",
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        nullptr
    };

    for (const char** p = list; *p; ++p) {
        if (strcasecmp(*p, name) == 0) return true;
    }
    return false;
}

WebOSClient::WebOSClient() = default;

void WebOSClient::begin(const char* host, uint16_t port, bool useTLS, const char* clientKey) {
    _host      = host;
    _port      = port;
    _useTLS    = useTLS;
    _clientKey = clientKey ? clientKey : "";
}

void WebOSClient::connect() {
    if (_state != State::IDLE) return;

    Serial.printf("[WebOS] Connecting to %s:%u (TLS=%d)\n", _host.c_str(), _port, _useTLS);

    _ws.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
        handleSsapEvent(type, payload, length);
    });

    if (_useTLS) {
        _ws.beginSSL(_host.c_str(), _port, "/");
    } else {
        _ws.begin(_host.c_str(), _port, "/");
    }

    _ws.setReconnectInterval(0);
    _wsActive = true;
    _state = State::CONNECTING;
}

void WebOSClient::disconnect() {
    _ws.disconnect();
    _pointerWs.disconnect();
    _wsActive = false;
    _pointerActive = false;
    _state = State::IDLE;
    _registered = false;
    _pointerConnected = false;
    _pointerInitialized = false;
    _pendingPointerRequestId = "";
    _pendingLaunchRequestId = "";
    _foregroundAppSubscriptionId = "";
    _fragmentBuffer = "";
    _receivingTextFragment = false;
}

void WebOSClient::loop() {
    if (_wsActive) _ws.loop();
    if (_pointerInitialized && _pointerActive) _pointerWs.loop();
}

String WebOSClient::nextId(const char* prefix) {
    ++_msgId;
    String s = prefix;
    s += "_";
    s += _msgId;
    return s;
}

void WebOSClient::sendRegister() {
    JsonDocument payloadDoc;
    DeserializationError err = deserializeJson(payloadDoc, REGISTER_MANIFEST);
    if (err) {
        Serial.printf("[WebOS] Manifest parse error: %s\n", err.c_str());
        return;
    }

    if (_clientKey.length() > 0) {
        payloadDoc["client-key"] = _clientKey;
    }

    String payloadStr;
    serializeJson(payloadDoc, payloadStr);

    _registeredWithExistingKey = (_clientKey.length() > 0);

    String msg;
    msg.reserve(payloadStr.length() + 64);
    msg  = "{\"id\":\"";
    msg += nextId("register");
    msg += "\",\"type\":\"register\",\"payload\":";
    msg += payloadStr;
    msg += "}";

    Serial.printf("[WebOS] -> register (%u bytes, existingKey=%d)\n",
                  (unsigned)msg.length(), (int)_registeredWithExistingKey);
    _ws.sendTXT(msg);
    _state = State::REGISTERING;
}

void WebOSClient::handleSsapEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            Serial.println("[WebOS] WS connected");
            sendRegister();
            break;

        case WStype_DISCONNECTED:
            Serial.println("[WebOS] WS disconnected");
            _wsActive = false;
            _registered = false;
            _state = State::IDLE;
            _fragmentBuffer = "";
            _receivingTextFragment = false;
            if (_onDisconnected) _onDisconnected();
            break;

        case WStype_TEXT: {
            _fragmentBuffer = "";
            _receivingTextFragment = false;
            String msg((const char*)payload, length);
            if (msg.length() <= 400) {
                Serial.printf("[WebOS] <- %s\n", msg.c_str());
            } else {
                Serial.printf("[WebOS] <- %.400s... (%u bytes)\n",
                              msg.c_str(), (unsigned)msg.length());
            }
            handleSsapMessage(msg);
            break;
        }

        case WStype_FRAGMENT_TEXT_START:
            _fragmentBuffer = String((const char*)payload, length);
            _receivingTextFragment = true;
            Serial.printf("[WebOS] <- text fragment start (%u bytes)\n", (unsigned)length);
            break;

        case WStype_FRAGMENT:
            if (_receivingTextFragment) {
                _fragmentBuffer += String((const char*)payload, length);
            }
            break;

        case WStype_FRAGMENT_FIN:
            if (_receivingTextFragment) {
                _fragmentBuffer += String((const char*)payload, length);
                Serial.printf("[WebOS] <- fragmented text assembled (%u bytes)\n",
                              (unsigned)_fragmentBuffer.length());
                handleSsapMessage(_fragmentBuffer);
                _fragmentBuffer = "";
                _receivingTextFragment = false;
            }
            break;

        case WStype_FRAGMENT_BIN_START:
            _fragmentBuffer = "";
            _receivingTextFragment = false;
            Serial.printf("[WebOS] Ignoring binary fragment (%u bytes)\n", (unsigned)length);
            break;

        case WStype_ERROR:
            Serial.println("[WebOS] WS error");
            break;

        default:
            break;
    }
}

void WebOSClient::handleSsapMessage(const String& msg) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, msg);
    if (err) {
        Serial.printf("[WebOS] JSON parse error: %s\n", err.c_str());
        return;
    }

    const char* type = doc["type"] | "";
    const char* id   = doc["id"] | "";

    if (strcmp(type, "registered") == 0) {
        const char* key = doc["payload"]["client-key"] | "";
        Serial.printf("[WebOS] Registered! client-key=%s\n", key);
        if (key && *key) {
            _clientKey = key;
            if (_onNewClientKey) _onNewClientKey(_clientKey);
        }

        _registered = true;
        _state = State::READY;
        if (_onReady) _onReady();
        return;
    }

    if (strcmp(type, "response") == 0 && strncmp(id, "listapps_", 9) == 0) {
        JsonArray arr = doc["payload"]["apps"].as<JsonArray>();
        Serial.println("[WebOS] Apps:");

        for (JsonObject item : arr) {
            const char* appId = item["id"] | "";
            const char* title = item["title"] | "";
            Serial.printf("  title='%s'  id='%s'\n", title, appId);
        }

        disconnect();
        return;
    }

    if (strcmp(type, "response") == 0 && strncmp(id, "listlp_", 7) == 0) {
        JsonArray arr = doc["payload"]["launchPoints"].as<JsonArray>();
        Serial.println("[WebOS] Launch points:");

        for (JsonObject item : arr) {
            const char* lpId  = item["id"] | "";
            const char* title = item["title"] | "";
            const char* appId = item["appId"] | "";

            Serial.printf("  title='%s'  id='%s'  appId='%s'\n", title, lpId, appId);
        }

        disconnect();
        return;
    }

    if (_foregroundAppSubscriptionId.length() > 0 &&
        _foregroundAppSubscriptionId == id &&
        strcmp(type, "response") == 0) {
        JsonVariant fgArray = doc["payload"]["foregroundAppInfo"];
        if (!fgArray.isNull()) {
            Serial.println("[WebOS] Foreground apps:");
            for (JsonObject item : fgArray.as<JsonArray>()) {
                const char* appId = item["appId"] | "";
                const char* launchPointId = item["launchPointId"] | "";
                const char* windowType = item["windowType"] | "";
                bool primary = item["primary"] | false;
                Serial.printf("  primary=%d  appId='%s'  launchPointId='%s'  windowType='%s'\n",
                              (int)primary, appId, launchPointId, windowType);
            }
        } else {
            const char* appId = doc["payload"]["appId"] | "";
            const char* processId = doc["payload"]["processId"] | "";
            Serial.printf("[WebOS] Foreground app: appId='%s'  processId='%s'\n",
                          appId, processId);
        }
        return;
    }

    if (_pendingLaunchRequestId.length() > 0 && _pendingLaunchRequestId == id &&
        strcmp(type, "response") == 0) {
        bool ok = doc["payload"]["returnValue"] | false;
        if (ok) {
            Serial.println("[WebOS] Launch accepted by TV");
            if (_onLaunchResult) _onLaunchResult(true, "");
        } else {
            Serial.println("[WebOS] Launch response without success");
            if (_onLaunchResult) _onLaunchResult(false, "returnValue=false");
        }
        _pendingLaunchRequestId = "";
        return;
    }

    if (strcmp(type, "response") == 0 &&
        doc["payload"]["pairingType"].is<const char*>()) {
        Serial.println("[WebOS] Please confirm pairing on TV screen!");
        return;
    }

    if (strcmp(type, "error") == 0) {
        Serial.printf("[WebOS] Error from TV: %s\n", msg.c_str());
        const char* errStr = doc["error"] | "";

        if (_pendingLaunchRequestId.length() > 0 && _pendingLaunchRequestId == id) {
            if (_onLaunchResult) _onLaunchResult(false, String(errStr));
            _pendingLaunchRequestId = "";
        }

        if (_registered && strstr(errStr, "401") != nullptr) {
            if (_registeredWithExistingKey) {
                Serial.println("[WebOS] Stale client-key detected - will re-pair");
                _registered = false;
                _clientKey = "";
                if (_onInvalidClientKey) _onInvalidClientKey();
            } else {
                Serial.println("[WebOS] 401 on freshly-paired key - manifest/permission issue, NOT wiping");
            }
        }
        return;
    }

    if (_pendingPointerRequestId.length() > 0 && _pendingPointerRequestId == id) {
        const char* socketPath = doc["payload"]["socketPath"] | "";
        Serial.printf("[WebOS] Pointer socket URL: %s\n", socketPath);
        if (*socketPath) openPointerSocket(socketPath);
        _pendingPointerRequestId = "";
        return;
    }
}

bool WebOSClient::listApps() {
    if (!_registered) {
        Serial.println("[WebOS] listApps: not registered");
        return false;
    }

    JsonDocument out;
    out["id"]   = nextId("listapps");
    out["type"] = "request";
    out["uri"]  = "ssap://com.webos.applicationManager/listApps";

    String s;
    serializeJson(out, s);
    Serial.println("[WebOS] -> listApps");
    return _ws.sendTXT(s);
}

bool WebOSClient::listLaunchPoints() {
    if (!_registered) {
        Serial.println("[WebOS] listLaunchPoints: not registered");
        return false;
    }

    JsonDocument out;
    out["id"]   = nextId("listlp");
    out["type"] = "request";
    out["uri"]  = "ssap://com.webos.applicationManager/listLaunchPoints";

    String s;
    serializeJson(out, s);
    Serial.println("[WebOS] -> listLaunchPoints");
    return _ws.sendTXT(s);
}

bool WebOSClient::subscribeForegroundAppInfo(bool extraInfo) {
    if (!_registered) {
        Serial.println("[WebOS] subscribeForegroundAppInfo: not registered");
        return false;
    }

    JsonDocument out;
    _foregroundAppSubscriptionId = nextId("fgapp");
    out["id"]   = _foregroundAppSubscriptionId;
    out["type"] = "subscribe";
    out["uri"]  = "ssap://com.webos.applicationManager/getForegroundAppInfo";
    out["payload"]["subscribe"] = true;
    out["payload"]["extraInfo"] = extraInfo;

    String s;
    serializeJson(out, s);
    Serial.printf("[WebOS] -> subscribeForegroundAppInfo (extraInfo=%d)\n", (int)extraInfo);
    return _ws.sendTXT(s);
}

void WebOSClient::requestPointerSocket() {
    if (!_registered) return;

    _pendingPointerRequestId = nextId("ptr");

    JsonDocument out;
    out["id"]   = _pendingPointerRequestId;
    out["type"] = "request";
    out["uri"]  = "ssap://com.webos.service.networkinput/getPointerInputSocket";

    String s;
    serializeJson(out, s);
    Serial.println("[WebOS] -> getPointerInputSocket");
    _ws.sendTXT(s);
    _state = State::REQUESTING_POINTER;
}

void WebOSClient::openPointerSocket(const String& url) {
    String u = url;
    bool tls = u.startsWith("wss://");
    int schemeEnd = u.indexOf("://");
    if (schemeEnd < 0) {
        Serial.println("[WebOS] Bad pointer URL");
        return;
    }

    String rest = u.substring(schemeEnd + 3);
    int slash = rest.indexOf('/');
    String hostport = (slash >= 0) ? rest.substring(0, slash) : rest;
    String path     = (slash >= 0) ? rest.substring(slash) : "/";

    String host;
    uint16_t port = 0;
    int colon = hostport.indexOf(':');
    if (colon >= 0) {
        host = hostport.substring(0, colon);
        port = hostport.substring(colon + 1).toInt();
    } else {
        host = hostport;
        port = tls ? 443 : 80;
    }

    _pointerHost   = host;
    _pointerPort   = port;
    _pointerPath   = path;
    _pointerUseTLS = tls;

    _pointerWs.onEvent([this](WStype_t t, uint8_t* p, size_t l) {
        handlePointerEvent(t, p, l);
    });

    Serial.printf("[WebOS] Opening pointer socket %s:%u%s (tls=%d)\n",
                  host.c_str(), port, path.c_str(), tls);

    if (tls) {
        _pointerWs.beginSSL(host.c_str(), port, path.c_str());
    } else {
        _pointerWs.begin(host.c_str(), port, path.c_str());
    }

    _pointerWs.setReconnectInterval(0);
    _pointerInitialized = true;
    _pointerActive = true;
}

void WebOSClient::handlePointerEvent(WStype_t type, uint8_t* payload, size_t length) {
    (void)payload;
    (void)length;

    switch (type) {
        case WStype_CONNECTED:
            Serial.println("[Pointer] connected");
            _pointerConnected = true;
            break;

        case WStype_DISCONNECTED:
            Serial.println("[Pointer] disconnected");
            _pointerActive = false;
            _pointerConnected = false;
            break;

        case WStype_TEXT:
            break;

        default:
            break;
    }
}

bool WebOSClient::launchApp(const char* appId) {
    if (!_registered) {
        Serial.println("[WebOS] launchApp: not registered");
        return false;
    }

    if (!_pointerInitialized && _pendingPointerRequestId.length() == 0) {
        requestPointerSocket();
    }

    JsonDocument out;
    _pendingLaunchRequestId = nextId("launch");
    out["id"]   = _pendingLaunchRequestId;
    out["type"] = "request";
    out["uri"]  = "ssap://system.launcher/launch";
    out["payload"]["id"] = appId;

    String s;
    serializeJson(out, s);
    Serial.printf("[WebOS] -> launch %s\n", appId);
    return _ws.sendTXT(s);
}

bool WebOSClient::sendButton(const char* button) {
    if (!_pointerConnected) {
        Serial.printf("[WebOS] sendButton(%s): pointer not connected\n", button);
        return false;
    }

    if (!isPointerButton(button)) {
        Serial.printf("[WebOS] sendButton: unknown button %s\n", button);
        return false;
    }

    String msg = "type:button\nname:";
    msg += button;
    msg += "\n\n";
    Serial.printf("[Pointer] -> %s\n", button);
    return _pointerWs.sendTXT(msg);
}
