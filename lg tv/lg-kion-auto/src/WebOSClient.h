#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <functional>

// Клиент WebOS SSAP для LG TV.
// Поддерживает:
//   - регистрацию (получение client-key)
//   - запуск приложений
//   - виртуальный пульт (стрелки/OK) через отдельный pointer socket
class WebOSClient {
public:
    using DoneCallback = std::function<void(bool success)>;

    WebOSClient();

    bool listApps();
    bool listLaunchPoints();
    bool subscribeForegroundAppInfo(bool extraInfo = true);

    // Настройка подключения. clientKey может быть пустым — тогда
    // TV попросит подтвердить pairing на экране и вернёт новый ключ.
    void begin(const char* host, uint16_t port, bool useTLS, const char* clientKey);

    // Должен вызываться из loop()
    void loop();

    // Старт подключения. После успешной регистрации вызовет onReady.
    void connect();
    void disconnect();

    bool isRegistered() const { return _registered; }
    bool isPointerConnected() const { return _pointerConnected; }

    // Команды высокого уровня. Возвращают true, если команда отправлена
    // (не обязательно что TV уже отработал).
    bool launchApp(const char* appId);
    bool sendButton(const char* button);   // "UP","DOWN","LEFT","RIGHT","ENTER",...

    // Коллбеки
    void onReady(std::function<void()> cb)               { _onReady = cb; }
    void onDisconnected(std::function<void()> cb)        { _onDisconnected = cb; }
    void onNewClientKey(std::function<void(const String&)> cb) { _onNewClientKey = cb; }
    void onLaunchResult(std::function<void(bool, const String&)> cb) { _onLaunchResult = cb; }
    // Вызывается, если TV отвечает 401 после успешной регистрации —
    // значит сохранённый client-key на TV привязан к урезанным правам
    // и его надо выкинуть, чтобы получить новый с полным манифестом.
    void onInvalidClientKey(std::function<void()> cb)    { _onInvalidClientKey = cb; }

private:
    // --- Состояния основного сокета ---
    enum class State {
        IDLE,
        CONNECTING,
        REGISTERING,
        READY,
        REQUESTING_POINTER,
    };

    // --- Основной WebSocket (SSAP) ---
    WebSocketsClient _ws;
    State            _state = State::IDLE;
    bool             _registered = false;
    bool             _wsActive = false;

    String           _host;
    uint16_t         _port;
    bool             _useTLS;
    String           _clientKey;

    uint32_t         _msgId = 0;
    String           _pendingPointerRequestId;
    String           _pendingLaunchRequestId;
    String           _foregroundAppSubscriptionId;
    String           _fragmentBuffer;
    bool             _receivingTextFragment = false;
    // true, если в последнем register мы передали непустой client-key.
    // Нужно, чтобы отличать "стейл-ключ из NVS" от "свежая пара, но TV всё
    // равно вернул 401" — в последнем случае вайпать бесполезно, получим цикл.
    bool             _registeredWithExistingKey = false;

    // --- Pointer socket (для кнопок) ---
    WebSocketsClient _pointerWs;
    bool             _pointerConnected = false;
    bool             _pointerInitialized = false;
    bool             _pointerActive = false;
    String           _pointerHost;
    uint16_t         _pointerPort = 0;
    String           _pointerPath;
    bool             _pointerUseTLS = false;

    // Callbacks
    std::function<void()>                 _onReady;
    std::function<void()>                 _onDisconnected;
    std::function<void(const String&)>    _onNewClientKey;
    std::function<void(bool, const String&)> _onLaunchResult;
    std::function<void()>                 _onInvalidClientKey;

    // --- Internal ---
    void handleSsapEvent(WStype_t type, uint8_t* payload, size_t length);
    void handlePointerEvent(WStype_t type, uint8_t* payload, size_t length);
    void handleSsapMessage(const String& msg);
    void sendRegister();
    void requestPointerSocket();
    void openPointerSocket(const String& url);
    String nextId(const char* prefix);
};
